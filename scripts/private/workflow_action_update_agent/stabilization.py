#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import json
import os
import shutil
import tempfile
import time
import urllib.parse
from pathlib import Path

from agent_runtime.contracts import AgentInstance
from agent_runtime.github_actions import find_latest_workflow_run_for_head, read_pr_details
from github_api import github_api_json
from agent_runtime.review.state import (
    missing_review_findings_error_message,
    normalize_review_state,
    read_json_file,
    resolve_canonical_review_state,
    review_state_can_drive_stabilization,
    review_state_matches_head,
    review_state_recommendation,
    review_state_requires_findings,
)

from .github_workflows import (
    build_validation_dispatch_context,
    dispatch_stabilizer_workflow,
    ensure_validation_workflow_run,
    merge_pr,
)
from .runtime import (
    CONTEXT_ROOT_TOKEN,
    PR_NUMBER_TOKEN,
    REPAIR_BRANCH_TOKEN,
    REVIEW_RECOMMENDATION_TOKEN,
    REVIEW_RUN_ID_TOKEN,
    REVIEW_STATE_JSON_TOKEN,
    REVIEW_SUMMARY_TOKEN,
    REVIEW_WORKFLOW_NAME_TOKEN,
    SOURCE_RUN_ID_TOKEN,
    STABILIZATION_MAX_ATTEMPTS,
    WAIT_TIMEOUT_SECONDS,
    load_profile,
    profile_agent_model,
    profile_prompt_replacements,
    profile_string_list,
    profile_validation_workflows,
    render_markdown_template,
    resolve_repo_path,
    run_command,
    run_validation_command,
    validation_command_environment,
    workflow_allowed_review_recommendations,
    write_json_file,
    write_outputs,
)


def read_review_state(*, state_script: str, pr_number: str) -> dict[str, object]:
    script_path = resolve_repo_path(state_script)
    if not script_path.is_file():
        raise ValueError(f"Review state script is missing: {script_path}")

    env = dict(os.environ)
    if not env.get("GITHUB_TOKEN"):
        env["GITHUB_TOKEN"] = env.get("GH_TOKEN", "")
    env["GITHUB_PR_NUMBER"] = pr_number

    with tempfile.TemporaryDirectory(prefix="workflow-action-update-agent-review-") as temp_dir:
        output_path = Path(temp_dir) / "review-state.json"
        run_command(
            ["python3", str(script_path), "--output", str(output_path)],
            env=env,
        )
        return read_json_file(output_path)


def resolve_review_state_for_run(
    *,
    repository: str,
    pr_number: str,
    workflow_name: str,
    run_id: str,
    head_sha: str,
    fallback_state: dict[str, object],
) -> tuple[dict[str, object], str]:
    if not repository or not run_id:
        return {}, ""
    return resolve_canonical_review_state(
        repository=repository,
        pr_number=pr_number,
        workflow_name=workflow_name,
        run_id=run_id,
        head_sha=head_sha,
        fallback_state=fallback_state,
    )


def wait_for_review_state(
    *,
    pr_number: str,
    workflow_name: str,
    review_state_script: str,
    expected_run_id: str,
    head_sha: str,
) -> dict[str, object]:
    deadline = time.time() + WAIT_TIMEOUT_SECONDS
    last_non_actionable_state: dict[str, object] = {}

    while time.time() < deadline:
        latest_artifact_state = normalize_review_state(
            read_review_state(
                state_script=review_state_script,
                pr_number=pr_number,
            )
        )
        observed_run_id = str(latest_artifact_state.get("run_id") or "")
        observed_head_sha = str(latest_artifact_state.get("head_sha") or "")
        repository = os.environ.get("GITHUB_REPOSITORY", "")

        canonical_state, source = resolve_review_state_for_run(
            repository=repository,
            pr_number=pr_number,
            workflow_name=workflow_name,
            run_id=expected_run_id,
            head_sha=head_sha,
            fallback_state=latest_artifact_state,
        )
        if canonical_state:
            recommendation = review_state_recommendation(canonical_state)
            print(
                f"Observed {workflow_name} recommendation {recommendation} from {source} "
                f"for run {expected_run_id} on PR #{pr_number}"
            )
            return canonical_state

        if review_state_matches_head(latest_artifact_state, run_id=expected_run_id, head_sha=head_sha):
            if review_state_requires_findings(latest_artifact_state, source="artifact"):
                last_non_actionable_state = latest_artifact_state
            elif review_state_can_drive_stabilization(latest_artifact_state, source="artifact"):
                recommendation = review_state_recommendation(latest_artifact_state)
                print(
                    f"Observed {workflow_name} recommendation {recommendation} from artifact "
                    f"for run {expected_run_id} on PR #{pr_number}"
                )
                return latest_artifact_state

        if observed_run_id != expected_run_id:
            time.sleep(15)
            continue
        if observed_head_sha != head_sha:
            time.sleep(15)
            continue

        time.sleep(15)

    message = f"Timed out waiting for {workflow_name} canonical review state on PR #{pr_number}"
    if last_non_actionable_state:
        message += "\n" + missing_review_findings_error_message(
            pr_number=pr_number,
            workflow_name=workflow_name,
            review_state=last_non_actionable_state,
            source="artifact",
        )
    raise RuntimeError(message)


def publish_review_state_to_pr(
    *,
    pr_number: str,
    head_sha: str,
    review_state: dict[str, object],
) -> None:
    script_path = resolve_repo_path("scripts/private/agent_runtime/review/publish.py")
    if not script_path.is_file():
        raise ValueError(f"Agent review publish script is missing: {script_path}")

    env = dict(os.environ)
    if not env.get("GITHUB_TOKEN"):
        env["GITHUB_TOKEN"] = env.get("GH_TOKEN", "")
    if not env.get("GITHUB_TOKEN"):
        raise RuntimeError("GITHUB_TOKEN or GH_TOKEN is required to publish Agent review state.")
    env["GITHUB_PR_NUMBER"] = pr_number
    env["GITHUB_HEAD_SHA"] = head_sha
    env["GITHUB_RUN_ID"] = str(review_state.get("run_id") or "")

    with tempfile.TemporaryDirectory(prefix="workflow-action-update-agent-publish-review-") as temp_dir:
        input_path = Path(temp_dir) / "review.json"
        markdown_path = Path(temp_dir) / "review-summary.md"
        write_json_file(input_path, review_state)
        run_command(
            [
                "python3",
                str(script_path),
                "--input",
                str(input_path),
                "--markdown-out",
                str(markdown_path),
                "--publish-pr-comment",
            ],
            env=env,
        )


def ensure_allowed_review_recommendation(
    *,
    pr_number: str,
    workflow_name: str,
    review_state: dict[str, object],
    allowed_review_recommendations: list[str],
) -> None:
    recommendation = str(review_state.get("overall_recommendation") or "").strip().lower()
    if recommendation in allowed_review_recommendations:
        return
    raise RuntimeError(
        f"{workflow_name} recommendation for PR #{pr_number} was '{recommendation}', "
        f"expected one of {', '.join(allowed_review_recommendations)}.",
    )


def split_validation_workflows(
    profile: dict[str, object],
) -> tuple[dict[str, object] | None, list[dict[str, object]]]:
    workflows = profile_validation_workflows(profile)
    review_workflows = [
        workflow
        for workflow in workflows
        if str(workflow.get("review_state_script") or "")
    ]
    if len(review_workflows) > 1:
        raise ValueError("Only one validation workflow with review_state_script is supported.")

    review_workflow = review_workflows[0] if review_workflows else None
    other_workflows = [
        workflow
        for workflow in workflows
        if workflow is not review_workflow
    ]
    return review_workflow, other_workflows


def build_stabilize_prompt(
    *,
    profile: dict[str, object],
    context_root: Path,
    pr_number: str,
    repair_branch: str,
    source_run_id: str,
    workflow_name: str,
    review_state: dict[str, object],
) -> str:
    review_summary = str(review_state.get("summary") or "").strip() or "No summary provided."
    review_recommendation = str(review_state.get("overall_recommendation") or "").strip() or "unknown"
    review_run_id = str(review_state.get("run_id") or "").strip() or "unknown"
    review_state_json = json.dumps(review_state, indent=2, sort_keys=True)
    replacements = profile_prompt_replacements(profile)
    replacements.update(
        {
            CONTEXT_ROOT_TOKEN: context_root.as_posix(),
            PR_NUMBER_TOKEN: pr_number,
            REPAIR_BRANCH_TOKEN: repair_branch,
            REVIEW_RECOMMENDATION_TOKEN: review_recommendation,
            REVIEW_RUN_ID_TOKEN: review_run_id,
            REVIEW_STATE_JSON_TOKEN: review_state_json,
            REVIEW_SUMMARY_TOKEN: review_summary,
            REVIEW_WORKFLOW_NAME_TOKEN: workflow_name,
            SOURCE_RUN_ID_TOKEN: source_run_id,
        }
    )

    return render_markdown_template(
        "stabilize-goal.md.in",
        replacements,
    )


def write_stabilization_context(
    *,
    context_root: Path,
    review_state: dict[str, object],
    prompt_text: str,
) -> Path:
    context_root.mkdir(parents=True, exist_ok=True)
    write_json_file(context_root / "review-state.json", review_state)
    prompt_path = context_root / "stabilize-goal.md"
    prompt_path.write_text(prompt_text, encoding="utf-8")
    return prompt_path


def copy_required_path(source: Path, destination: Path) -> None:
    if not source.exists():
        raise ValueError(f"Required helper bundle source is missing: {source}")
    if source.is_dir():
        shutil.copytree(source, destination, dirs_exist_ok=True)
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def snapshot_helper_bundle(*, bundle_root: Path) -> None:
    if bundle_root.exists():
        shutil.rmtree(bundle_root)

    copy_required_path(resolve_repo_path("scripts/private/github_api.py"),
                       bundle_root / "scripts/private/github_api.py")
    copy_required_path(
        resolve_repo_path("scripts/private/workflow_action_update_agent"),
        bundle_root / "scripts/private/workflow_action_update_agent",
    )
    copy_required_path(
        resolve_repo_path("scripts/private/agent_runtime"),
        bundle_root / "scripts/private/agent_runtime",
    )
    for runtime_file in (
        "requirements-openai-agents.txt",
        "agent-models.json",
        "agent-tasks.json",
    ):
        copy_required_path(
            resolve_repo_path(f".github/agent-runtime/runtime/{runtime_file}"),
            bundle_root / f".github/agent-runtime/runtime/{runtime_file}",
        )
    for directory in (
        ".github/agent-runtime/review/prompts",
        ".github/agent-runtime/review/schemas",
        ".github/agent-runtime/workflow-action-update-agent/prompts",
        ".github/agent-runtime/workflow-action-update-agent/profiles",
    ):
        copy_required_path(resolve_repo_path(directory), bundle_root / directory)


def restore_helper_bundle(*, bundle_root: Path, helper_root: Path) -> None:
    if not bundle_root.is_dir():
        raise ValueError(f"Workflow helper bundle is missing: {bundle_root}")
    exclude_file = Path(".git/info/exclude")
    helper_pattern = f"/{helper_root.as_posix().strip('/')}/"
    existing_excludes = exclude_file.read_text(encoding="utf-8") if exclude_file.is_file() else ""
    if helper_pattern not in existing_excludes.splitlines():
        exclude_file.parent.mkdir(parents=True, exist_ok=True)
        with exclude_file.open("a", encoding="utf-8") as output:
            output.write(f"{helper_pattern}\n")
    if helper_root.exists():
        shutil.rmtree(helper_root)
    shutil.copytree(bundle_root, helper_root)


def run_validation_commands(commands: list[str]) -> None:
    env = validation_command_environment()
    for command in commands:
        print(f"Running validation command: {command}")
        run_validation_command(command, env=env)


def commit_review_fix(
    *,
    pr_number: str,
    repair_branch: str,
    ticket_id: str,
    review_state: dict[str, object],
) -> str:
    push_token = os.environ.get("GH_TOKEN", "").strip()
    if not push_token:
        raise RuntimeError(
            "EXPKITS_AGENT_TOKEN must be provided as GH_TOKEN when pushing stabilization commits."
        )
    repository = os.environ.get("GITHUB_REPOSITORY", "").strip()
    if not repository:
        raise RuntimeError("GITHUB_REPOSITORY is required to push stabilization commits.")
    server_url = os.environ.get("GITHUB_SERVER_URL", "https://github.com").strip()
    parsed_server_url = urllib.parse.urlparse(server_url)
    if parsed_server_url.scheme != "https" or not parsed_server_url.netloc:
        raise RuntimeError(f"Unsupported GITHUB_SERVER_URL for token-authenticated push: {server_url}")
    user_payload = github_api_json("user")
    push_actor = str(user_payload.get("login") or "").strip() if isinstance(user_payload, dict) else ""
    if not push_actor:
        raise RuntimeError("Unable to resolve PAT owner login for stabilization push.")

    run_command(["git", "config", "user.name", "github-actions[bot]"])
    run_command(["git", "config", "user.email", "41898282+github-actions[bot]@users.noreply.github.com"])
    run_command(
        [
            "git",
            "remote",
            "set-url",
            "origin",
            f"https://{push_actor}:{push_token}@{parsed_server_url.netloc}/{repository}.git",
        ]
    )
    run_command(["git", "add", "-A"])
    if run_command(["git", "diff", "--cached", "--quiet"], check=False).returncode == 0:
        return ""

    commit_command = [
        "git",
        "commit",
        "-m",
        f"[bot] Address Agent Review findings on PR #{pr_number}",
        "-m",
        f"Task: {ticket_id}",
    ]

    review_run_id = str(review_state.get("run_id") or "").strip()
    if review_run_id:
        commit_command.extend(["-m", f"Agent Review run: {review_run_id}"])

    review_summary = str(review_state.get("summary") or "").strip()
    if review_summary:
        commit_command.extend(["-m", review_summary])

    run_command(commit_command)
    run_command(["git", "push", "origin", f"HEAD:{repair_branch}"])
    return run_command(["git", "rev-parse", "HEAD"], capture_output=True).stdout.strip()


def command_resolve_pr_details(args: argparse.Namespace) -> int:
    write_outputs(read_pr_details(args.pr_number), args.github_output)
    return 0


def command_prepare_stabilization_context(args: argparse.Namespace) -> int:
    profile = load_profile(args.profile_path)
    context_root = Path(args.context_root)
    review_workflow, _ = split_validation_workflows(profile)
    if review_workflow is None:
        raise RuntimeError("Stabilization requires a validation workflow with review_state_script.")
    repository = os.environ.get("GITHUB_REPOSITORY", "")

    pr_details = read_pr_details(args.pr_number)
    repair_branch = pr_details["repair_branch"]
    head_sha = args.head_sha or pr_details["head_sha"]
    if not repair_branch or not head_sha:
        raise RuntimeError(f"Unable to resolve repair branch and head SHA for PR #{args.pr_number}.")

    review_state = normalize_review_state(
        read_review_state(
            state_script=str(review_workflow["review_state_script"]),
            pr_number=args.pr_number,
        )
    )
    review_state_source = "artifact"
    review_head_sha = str(review_state.get("head_sha") or "").strip()
    recommendation = str(review_state.get("overall_recommendation") or "").strip().lower()
    review_run_id = str(review_state.get("run_id") or "").strip()

    if recommendation and review_head_sha == head_sha and review_run_id:
        canonical_state, canonical_source = resolve_review_state_for_run(
            repository=repository,
            pr_number=args.pr_number,
            workflow_name=str(review_workflow["workflow_name"]),
            run_id=review_run_id,
            head_sha=head_sha,
            fallback_state=review_state,
        )
        if canonical_state:
            review_state = canonical_state
            review_state_source = canonical_source
            review_head_sha = str(review_state.get("head_sha") or "").strip()
            recommendation = review_state_recommendation(review_state)

    if (not recommendation or not review_head_sha or review_head_sha != head_sha) and repository:
        review_run_id = find_latest_workflow_run_for_head(
            repository=repository,
            workflow_file=str(review_workflow["workflow_file"]),
            branch=repair_branch,
            head_sha=head_sha,
        )
        if review_run_id:
            canonical_state, canonical_source = resolve_review_state_for_run(
                repository=repository,
                pr_number=args.pr_number,
                workflow_name=str(review_workflow["workflow_name"]),
                run_id=review_run_id,
                head_sha=head_sha,
                fallback_state=review_state,
            )
            if canonical_state:
                review_state = canonical_state
                review_state_source = canonical_source
                review_head_sha = str(review_state.get("head_sha") or "").strip()
                recommendation = review_state_recommendation(review_state)
    if not recommendation:
        raise RuntimeError(f"Latest review state for PR #{args.pr_number} did not contain a recommendation.")
    if not review_head_sha:
        raise RuntimeError(f"Latest review state for PR #{args.pr_number} did not contain a head SHA.")
    if review_head_sha != head_sha:
        raise RuntimeError(
            f"Latest review state head SHA {review_head_sha} did not match expected head SHA {head_sha} for PR #{args.pr_number}.",
        )
    if review_state_requires_findings(review_state, source=review_state_source):
        raise RuntimeError(
            missing_review_findings_error_message(
                pr_number=args.pr_number,
                workflow_name=str(review_workflow["workflow_name"]),
                review_state=review_state,
                source=review_state_source,
            )
        )

    write_stabilization_context(
        context_root=context_root,
        review_state=review_state,
        prompt_text=build_stabilize_prompt(
            profile=profile,
            context_root=context_root,
            pr_number=args.pr_number,
            repair_branch=repair_branch,
            source_run_id=args.source_run_id,
            workflow_name=str(review_workflow["workflow_name"]),
            review_state=review_state,
        ),
    )
    write_outputs(
        {
            "repair_branch": repair_branch,
            "head_sha": head_sha,
            "target_branch": pr_details["target_branch"],
            "review_recommendation": recommendation,
            "review_run_id": str(review_state.get("run_id") or "").strip(),
            "agent_model": profile_agent_model(profile, AgentInstance.STABILIZATION, args.profile_path),
        },
        args.github_output,
    )
    return 0


def command_snapshot_helper_bundle(args: argparse.Namespace) -> int:
    snapshot_helper_bundle(bundle_root=Path(args.bundle_root))
    return 0


def command_restore_helper_bundle(args: argparse.Namespace) -> int:
    restore_helper_bundle(
        bundle_root=Path(args.bundle_root),
        helper_root=Path(args.helper_root),
    )
    return 0


def command_run_validation(args: argparse.Namespace) -> int:
    profile = load_profile(args.profile_path)
    run_validation_commands(profile_string_list(profile, "validation_commands"))
    return 0


def command_commit_review_fix(args: argparse.Namespace) -> int:
    context_root = Path(args.context_root)
    review_state = read_json_file(context_root / "review-state.json")
    head_sha = commit_review_fix(
        pr_number=args.pr_number,
        repair_branch=args.repair_branch,
        ticket_id=args.ticket_id,
        review_state=review_state,
    )
    if not head_sha:
        raise RuntimeError(
            f"Agent produced no repository changes for PR #{args.pr_number} during stabilization.",
        )
    write_outputs({"head_sha": head_sha}, args.github_output)
    return 0


def command_stabilize_pr(args: argparse.Namespace) -> int:
    profile = load_profile(args.profile_path)
    repository = os.environ["GITHUB_REPOSITORY"]
    context_root = Path(args.context_root)
    dispatch_ref = os.environ.get("GITHUB_REF_NAME", "main")
    review_workflow, other_workflows = split_validation_workflows(profile)
    pr_details = read_pr_details(args.pr_number)
    repair_branch = pr_details["repair_branch"] or args.repair_branch
    target_branch = pr_details["target_branch"]
    head_sha = args.head_sha or pr_details["head_sha"]

    for attempt in range(1, STABILIZATION_MAX_ATTEMPTS + 1):
        print(f"Stabilization attempt {attempt}/{STABILIZATION_MAX_ATTEMPTS} for PR #{args.pr_number} at {head_sha}")
        dispatch_context = build_validation_dispatch_context(
            pr_number=args.pr_number,
            repair_branch=repair_branch,
            head_sha=head_sha,
            target_branch=target_branch,
            source_run_id=args.source_run_id,
            ticket_id=args.ticket_id,
        )

        if review_workflow is not None:
            review_run_id, review_run_event = ensure_validation_workflow_run(
                repository=repository,
                workflow=review_workflow,
                repair_branch=repair_branch,
                head_sha=head_sha,
                dispatch_context=dispatch_context,
            )
            review_state = wait_for_review_state(
                pr_number=args.pr_number,
                workflow_name=str(review_workflow["workflow_name"]),
                review_state_script=str(review_workflow["review_state_script"]),
                expected_run_id=review_run_id,
                head_sha=head_sha,
            )
            if review_run_event == "workflow_dispatch":
                publish_review_state_to_pr(
                    pr_number=args.pr_number,
                    head_sha=head_sha,
                    review_state=review_state,
                )
            allowed_recommendations = workflow_allowed_review_recommendations(review_workflow)
            recommendation = str(review_state.get("overall_recommendation") or "").strip().lower()
            if recommendation not in allowed_recommendations:
                dispatch_stabilizer_workflow(
                    repository=repository,
                    pr_number=args.pr_number,
                    head_sha=head_sha,
                    source_run_id=args.source_run_id,
                    ticket_id=args.ticket_id,
                    profile_path=args.profile_path,
                    context_root=str(context_root),
                    dispatch_ref=dispatch_ref,
                    dispatch_nonce=f"pr-{args.pr_number}-attempt-{attempt}-{int(time.time())}",
                )
                pr_details = read_pr_details(args.pr_number)
                repair_branch = pr_details["repair_branch"] or repair_branch
                target_branch = pr_details["target_branch"]
                head_sha = pr_details["head_sha"]
                # The stabilizer may have pushed a new commit. Restart the
                # loop so that the head gets a fresh Agent Review before merge.
                continue

            ensure_allowed_review_recommendation(
                pr_number=args.pr_number,
                workflow_name=str(review_workflow["workflow_name"]),
                review_state=review_state,
                allowed_review_recommendations=allowed_recommendations,
            )

        for workflow in other_workflows:
            ensure_validation_workflow_run(
                repository=repository,
                workflow=workflow,
                repair_branch=repair_branch,
                head_sha=head_sha,
                dispatch_context=dispatch_context,
            )

        if args.merge_when_stable:
            merge_pr(args.pr_number)
        else:
            print(f"PR #{args.pr_number} is stable; merge skipped.")
        return 0

    raise RuntimeError(
        f"Exceeded {STABILIZATION_MAX_ATTEMPTS} stabilization attempts for PR #{args.pr_number}.",
    )
