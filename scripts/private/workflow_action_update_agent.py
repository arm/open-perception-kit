#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import textwrap
import time
from datetime import datetime
from pathlib import Path


TICKET_RE = re.compile(r"^[A-Z][A-Z0-9]*-[0-9]+$")
REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_PROFILE_PATH = REPO_ROOT / ".github/ci/workflow-action-update-agent/profile.json"
PR_TEMPLATE_PATH = REPO_ROOT / ".github/PULL_REQUEST_TEMPLATE.md"
MARKDOWN_TEMPLATE_ROOT = REPO_ROOT / ".github/ci/workflow-action-update-agent"
PR_AUTOMATION_START = "<!-- workflow-action-update-agent:automation:start -->"
PR_AUTOMATION_END = "<!-- workflow-action-update-agent:automation:end -->"
PR_DESCRIPTION_START = "<!-- workflow-action-update-agent:description:start -->"
PR_DESCRIPTION_END = "<!-- workflow-action-update-agent:description:end -->"
DISPLAY_NAME_TOKEN = "{{DISPLAY_NAME}}"
PROMPT_CONTEXT_FILES_TOKEN = "{{PROMPT_CONTEXT_FILES}}"
VALIDATION_COMMANDS_TOKEN = "{{VALIDATION_COMMANDS}}"
WAIT_TIMEOUT_SECONDS = 1800


def run_command(
    args: list[str],
    *,
    capture_output: bool = False,
    check: bool = True,
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        check=check,
        text=True,
        capture_output=capture_output,
        env=env,
    )


def parse_json_command(args: list[str]) -> object:
    completed = run_command(args, capture_output=True)
    return json.loads(completed.stdout)


def write_outputs(values: dict[str, str], output_path: str | None = None) -> None:
    target = output_path or os.environ.get("GITHUB_OUTPUT")
    if not target:
        raise ValueError("GITHUB_OUTPUT is not set and no explicit output path was provided.")
    with Path(target).open("a", encoding="utf-8") as output_file:
        for key, value in values.items():
            output_file.write(f"{key}={value}\n")


def resolve_repo_path(path_value: str) -> Path:
    path = Path(path_value)
    if path.is_absolute():
        return path
    return (REPO_ROOT / path).resolve()


def load_json_file(path: Path) -> object:
    return json.loads(path.read_text(encoding="utf-8"))


def load_profile(profile_path: str = "") -> dict[str, object]:
    path = resolve_repo_path(profile_path or str(DEFAULT_PROFILE_PATH.relative_to(REPO_ROOT)))
    if not path.is_file():
        raise ValueError(f"Workflow action update agent profile is missing: {path}")

    profile = load_json_file(path)
    if not isinstance(profile, dict):
        raise ValueError(f"Workflow action update agent profile must be a JSON object: {path}")

    required_string_keys = (
        "display_name",
        "automation_name",
        "repair_branch_template",
        "repair_branch_guard_regex",
        "pr_trigger_label",
        "pr_title_template",
        "commit_subject_template",
        "commit_notes_template",
        "pr_description_template",
    )
    required_list_keys = (
        "prompt_context_files",
        "validation_commands",
        "validation_workflows",
    )

    for key in required_string_keys:
        profile_string(profile, key)
    for key in required_list_keys:
        profile_list(profile, key)

    profile_validation_workflows(profile)
    return profile


def profile_bool(profile: dict[str, object], key: str, default: bool) -> bool:
    value = profile.get(key, default)
    if not isinstance(value, bool):
        raise ValueError(f"Profile key '{key}' must be a boolean.")
    return value


def profile_string(profile: dict[str, object], key: str) -> str:
    value = profile.get(key)
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"Profile key '{key}' must be a non-empty string.")
    return value


def profile_optional_string(profile: dict[str, object], key: str, default: str = "") -> str:
    value = profile.get(key, default)
    if value in (None, ""):
        return default
    if not isinstance(value, str):
        raise ValueError(f"Profile key '{key}' must be a string when present.")
    return value.strip()


def profile_list(profile: dict[str, object], key: str) -> list[object]:
    value = profile.get(key)
    if not isinstance(value, list):
        raise ValueError(f"Profile key '{key}' must be a JSON array.")
    return value


def profile_string_list(profile: dict[str, object], key: str) -> list[str]:
    items = profile_list(profile, key)
    if not all(isinstance(item, str) and item.strip() for item in items):
        raise ValueError(f"Profile key '{key}' must contain only non-empty strings.")
    return [str(item) for item in items]


def profile_validation_workflows(profile: dict[str, object]) -> list[dict[str, object]]:
    workflows = profile_list(profile, "validation_workflows")
    parsed: list[dict[str, object]] = []
    for item in workflows:
        if not isinstance(item, dict):
            raise ValueError("Profile key 'validation_workflows' must contain only JSON objects.")
        workflow_file = item.get("workflow_file")
        workflow_name = item.get("workflow_name")
        review_state_script = item.get("review_state_script", "")
        allowed_review_recommendations = item.get("allowed_review_recommendations", [])
        if not isinstance(workflow_file, str) or not workflow_file.strip():
            raise ValueError("Each validation workflow must define a non-empty 'workflow_file'.")
        if not isinstance(workflow_name, str) or not workflow_name.strip():
            raise ValueError("Each validation workflow must define a non-empty 'workflow_name'.")
        if review_state_script and (not isinstance(review_state_script, str) or not review_state_script.strip()):
            raise ValueError(
                "Each validation workflow 'review_state_script' value must be a non-empty string when present.")
        if not isinstance(allowed_review_recommendations, list):
            raise ValueError(
                "Each validation workflow 'allowed_review_recommendations' value must be a JSON array when present.")
        if review_state_script:
            if not allowed_review_recommendations:
                raise ValueError(
                    "Each validation workflow with a 'review_state_script' must define 'allowed_review_recommendations'."
                )
            if not all(
                isinstance(recommendation, str) and recommendation.strip()
                for recommendation in allowed_review_recommendations
            ):
                raise ValueError(
                    "Each validation workflow 'allowed_review_recommendations' value must contain only non-empty strings."
                )
        elif allowed_review_recommendations:
            raise ValueError(
                "Each validation workflow with 'allowed_review_recommendations' must also define 'review_state_script'."
            )
        parsed.append(
            {
                "workflow_file": workflow_file,
                "workflow_name": workflow_name,
                "review_state_script": str(review_state_script or ""),
                "allowed_review_recommendations": [
                    str(recommendation).strip().lower()
                    for recommendation in allowed_review_recommendations
                ],
            }
        )
    return parsed


def format_profile_template(template: str, values: dict[str, str]) -> str:
    try:
        return template.format_map(values)
    except KeyError as exc:
        missing_key = exc.args[0]
        raise ValueError(f"Profile template is missing a value for '{missing_key}'.") from exc


def render_bullet_list(items: list[str]) -> str:
    return "\n".join(f"- `{item}`" for item in items)


def render_repair_ci_badge(repair_branch: str) -> str:
    return (
        "[![Perception Experience Kit CI Pipeline]"
        "(https://github.com/Arm-Debug/amp-dev-forge/actions/workflows/pek-ci.yml/badge.svg"
        f"?branch={repair_branch})"
        "(https://github.com/Arm-Debug/amp-dev-forge/actions/workflows/pek-ci.yml)"
    )


def load_markdown_template(name: str) -> str:
    template_path = MARKDOWN_TEMPLATE_ROOT / name
    if not template_path.is_file():
        raise ValueError(f"Markdown template file is missing: {template_path}")
    return template_path.read_text(encoding="utf-8")


def render_markdown_template(name: str, replacements: dict[str, str]) -> str:
    template = load_markdown_template(name)
    for token, replacement in replacements.items():
        template = template.replace(token, replacement)
    return template


def replace_marked_section(document: str, start_marker: str, end_marker: str, content: str) -> str:
    if start_marker not in document or end_marker not in document:
        raise ValueError(f"Template is missing marker section '{start_marker}' -> '{end_marker}'.")

    prefix, _, remainder = document.partition(start_marker)
    _, separator, suffix = remainder.partition(end_marker)
    if not separator:
        raise ValueError(f"Template is missing end marker '{end_marker}'.")

    return f"{prefix}{start_marker}\n{content.rstrip()}\n{end_marker}{suffix}"


def build_profile_context(
    profile: dict[str, object],
    *,
    source_run_id: str,
    source_run_url: str,
    source_workflow_name: str,
    repair_branch: str,
    target_branch: str,
    ticket_id: str,
) -> dict[str, str]:
    return {
        "automation_name": profile_string(profile, "automation_name"),
        "source_run_id": source_run_id,
        "source_run_url": source_run_url,
        "source_workflow_name": source_workflow_name,
        "repair_branch": repair_branch,
        "target_branch": target_branch,
        "ticket_id": ticket_id,
        "pr_trigger_label": profile_string(profile, "pr_trigger_label"),
    }


def render_pr_body_from_template(
    *,
    template_text: str,
    description: str,
    repair_branch: str,
    automation_name: str,
) -> str:
    automation_notice = textwrap.dedent(
        f"""
        Automation actor: `{automation_name}` bot run using `EXPKITS_AGENT_TOKEN`.
        GitHub may display the PAT owner as the PR author; this marker shows the PR was opened by automation.

        {render_repair_ci_badge(repair_branch)}
        """
    ).strip()

    rendered = replace_marked_section(template_text, PR_AUTOMATION_START, PR_AUTOMATION_END, automation_notice)
    rendered = replace_marked_section(
        rendered,
        PR_DESCRIPTION_START,
        PR_DESCRIPTION_END,
        textwrap.dedent(description).strip(),
    )
    return rendered.rstrip() + "\n"


def render_pr_body(*, profile: dict[str, object], description: str, repair_branch: str) -> str:
    if not PR_TEMPLATE_PATH.exists():
        raise ValueError(f"PR template file is missing: {PR_TEMPLATE_PATH}")

    return render_pr_body_from_template(
        template_text=PR_TEMPLATE_PATH.read_text(encoding="utf-8"),
        description=description,
        repair_branch=repair_branch,
        automation_name=profile_string(profile, "automation_name"),
    )


def render_repair_metadata_values(
    *,
    profile: dict[str, object],
    source_run_id: str,
    source_run_url: str,
    source_workflow_name: str,
    repair_branch: str,
    target_branch: str = "",
    ticket_id: str = "",
) -> tuple[str, str, str, str]:
    context = build_profile_context(
        profile,
        source_run_id=source_run_id,
        source_run_url=source_run_url,
        source_workflow_name=source_workflow_name,
        repair_branch=repair_branch,
        target_branch=target_branch,
        ticket_id=ticket_id,
    )
    description = format_profile_template(profile_string(profile, "pr_description_template"), context)
    pr_title = format_profile_template(profile_string(profile, "pr_title_template"), context)
    commit_subject = format_profile_template(profile_string(profile, "commit_subject_template"), context)
    commit_notes = format_profile_template(profile_string(profile, "commit_notes_template"), context)

    return (
        render_pr_body(profile=profile, description=description, repair_branch=repair_branch),
        pr_title + "\n",
        commit_subject + "\n",
        commit_notes,
    )


def write_repair_metadata_files(
    *,
    profile: dict[str, object],
    source_run_id: str,
    source_run_url: str,
    source_workflow_name: str,
    repair_branch: str,
    target_branch: str,
    ticket_id: str,
    body_file: str,
    pr_title_file: str,
    commit_subject_file: str,
    commit_notes_file: str,
) -> None:
    body, pr_title, commit_subject, commit_notes = render_repair_metadata_values(
        profile=profile,
        source_run_id=source_run_id,
        source_run_url=source_run_url,
        source_workflow_name=source_workflow_name,
        repair_branch=repair_branch,
        target_branch=target_branch,
        ticket_id=ticket_id,
    )
    Path(body_file).write_text(body, encoding="utf-8")
    Path(pr_title_file).write_text(pr_title, encoding="utf-8")
    Path(commit_subject_file).write_text(commit_subject, encoding="utf-8")
    Path(commit_notes_file).write_text(commit_notes, encoding="utf-8")


def parse_timestamp(value: str) -> datetime:
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def read_json_file(path: Path) -> dict[str, object]:
    payload = load_json_file(path)
    if not isinstance(payload, dict):
        raise ValueError(f"Expected JSON object in {path}.")
    return payload


def build_markdown_documents(
    *,
    profile: dict[str, object],
    source_run_id: str,
    source_run_url: str,
    source_workflow_name: str,
    target_branch: str,
    repair_branch: str,
    ticket_id: str,
    artifact_files: list[str],
) -> dict[str, str]:
    file_inventory = "# File Inventory\n\n" + "\n".join(f"- `{item}`" for item in artifact_files)
    prompt_context_files = render_bullet_list(profile_string_list(profile, "prompt_context_files"))
    validation_commands = render_bullet_list(profile_string_list(profile, "validation_commands"))

    return {
        "file-inventory.md": file_inventory,
        "failure-context.md": f"""
            # Failure Context

            - Source workflow: {source_workflow_name}
            - Source run ID: {source_run_id}
            - Source run URL: {source_run_url}
            - Repair target branch: {target_branch}
            - Planned repair branch: {repair_branch}
            - Ticket ID: {ticket_id}

            Primary evidence files:

            - `.codex/workflow-action-update-agent/source-run.json`
            - `.codex/workflow-action-update-agent/source-run.log`
            - `.codex/workflow-action-update-agent/file-inventory.md`

            Downloaded artifacts, if any, are under `.codex/workflow-action-update-agent/artifacts/`.
        """,
        "ponytail-review.md": load_markdown_template("ponytail-review.md"),
        "constraints.md": load_markdown_template("constraints.md"),
        "validation.md": render_markdown_template(
            "validation.md",
            {VALIDATION_COMMANDS_TOKEN: validation_commands},
        ),
        "goal.md": render_markdown_template(
            "goal.md",
            {
                DISPLAY_NAME_TOKEN: profile_string(profile, "display_name"),
                PROMPT_CONTEXT_FILES_TOKEN: prompt_context_files,
            },
        ),
    }


def command_resolve_inputs(args: argparse.Namespace) -> int:
    profile = load_profile(args.profile_path)
    ticket_id = args.ticket_id
    if not TICKET_RE.match(ticket_id):
        raise ValueError("ticket_id must match PROJECT-1234.")

    repository = os.environ["GITHUB_REPOSITORY"]
    current_ref_name = args.current_ref_name or "main"
    should_run = True
    skip_reason = ""
    source_run_id = args.source_run_id
    source_run_url = ""
    source_workflow_name = ""
    target_branch = args.target_branch or current_ref_name
    repair_branch = ""

    if not source_run_id:
        should_run = False
        skip_reason = "No source run ID was provided."
    else:
        run_json = parse_json_command(["gh", "api", f"repos/{repository}/actions/runs/{source_run_id}"])
        if not isinstance(run_json, dict):
            raise RuntimeError(f"Unexpected workflow run payload for source run {source_run_id}.")

        source_run_url = str(run_json.get("html_url") or "")
        source_workflow_name = str(run_json.get("name") or "")
        source_workflow_conclusion = str(run_json.get("conclusion") or "")
        source_head_branch = str(run_json.get("head_branch") or "")
        source_head_repository = str(dict(run_json.get("head_repository") or {}).get("full_name") or "")
        target_branch = args.target_branch or source_head_branch or current_ref_name

        repair_guard = re.compile(profile_string(profile, "repair_branch_guard_regex"))
        require_failure_conclusion = profile_bool(profile, "require_failure_conclusion", True)
        if require_failure_conclusion and source_workflow_conclusion != "failure":
            should_run = False
            skip_reason = f"Source workflow conclusion is '{source_workflow_conclusion}'."
        elif source_head_repository and source_head_repository != repository:
            should_run = False
            skip_reason = f"Source workflow head repository '{source_head_repository}' is not trusted."
        elif repair_guard.match(source_head_branch or ""):
            should_run = False
            skip_reason = "Source workflow already runs on a repair branch."
        else:
            repair_branch = format_profile_template(
                profile_string(profile, "repair_branch_template"),
                build_profile_context(
                    profile,
                    source_run_id=source_run_id,
                    source_run_url=source_run_url,
                    source_workflow_name=source_workflow_name,
                    repair_branch="",
                    target_branch=target_branch,
                    ticket_id=ticket_id,
                ),
            )

    write_outputs(
        {
            "should_run": "true" if should_run else "false",
            "skip_reason": skip_reason,
            "source_run_id": source_run_id or "",
            "source_run_url": source_run_url,
            "source_workflow_name": source_workflow_name,
            "target_branch": target_branch,
            "ticket_id": ticket_id,
            "repair_branch": repair_branch,
            "codex_model": profile_optional_string(profile, "codex_model"),
            "codex_effort": profile_optional_string(profile, "codex_effort"),
        },
        args.github_output,
    )
    return 0


def command_collect_context(args: argparse.Namespace) -> int:
    context_root = Path(args.context_root)
    if context_root.exists():
        shutil.rmtree(context_root)
    artifact_root = context_root / "artifacts"
    artifact_root.mkdir(parents=True, exist_ok=True)

    repository = os.environ["GITHUB_REPOSITORY"]
    run_json = parse_json_command(["gh", "api", f"repos/{repository}/actions/runs/{args.source_run_id}"])
    (context_root / "source-run.json").write_text(
        json.dumps(run_json, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    log_result = run_command(
        ["gh", "run", "view", args.source_run_id, "--repo", repository, "--log"],
        capture_output=True,
        check=False,
    )
    if log_result.returncode == 0:
        (context_root / "source-run.log").write_text(log_result.stdout, encoding="utf-8")
    else:
        (context_root / "source-run.log").write_text(
            f"Run logs were unavailable for {args.source_run_url}\n",
            encoding="utf-8",
        )

    run_command(
        ["gh", "run", "download", args.source_run_id, "--repo", repository, "--dir", str(artifact_root)],
        check=False,
    )
    return 0


def command_build_markdown(args: argparse.Namespace) -> int:
    profile = load_profile(args.profile_path)
    context_root = Path(args.context_root)
    artifact_files = sorted(
        path.relative_to(context_root).as_posix()
        for path in context_root.rglob("*")
        if path.is_file()
    )

    documents = build_markdown_documents(
        profile=profile,
        source_run_id=args.source_run_id,
        source_run_url=args.source_run_url,
        source_workflow_name=args.source_workflow_name,
        target_branch=args.target_branch,
        repair_branch=args.repair_branch,
        ticket_id=args.ticket_id,
        artifact_files=artifact_files,
    )

    for name, content in documents.items():
        (context_root / name).write_text(
            textwrap.dedent(content).strip() + "\n",
            encoding="utf-8",
        )
    return 0


def command_package_patch(args: argparse.Namespace) -> int:
    run_command(["git", "add", "-A"])

    diff_check = run_command(["git", "diff", "--cached", "--quiet"], check=False)
    if diff_check.returncode == 0:
        write_outputs({"has_changes": "false"}, args.github_output)
        return 0

    patch_file = Path(args.patch_file)
    diffstat_file = Path(args.diffstat_file)
    patch_file.parent.mkdir(parents=True, exist_ok=True)
    diffstat_file.parent.mkdir(parents=True, exist_ok=True)

    patch_result = run_command(["git", "diff", "--cached", "--binary"], capture_output=True)
    diffstat_result = run_command(["git", "diff", "--cached", "--stat"], capture_output=True)
    patch_file.write_text(patch_result.stdout, encoding="utf-8")
    diffstat_file.write_text(diffstat_result.stdout, encoding="utf-8")
    write_outputs({"has_changes": "true"}, args.github_output)
    return 0


def command_require_generated_patch(args: argparse.Namespace) -> int:
    print(f"Codex did not produce repository changes for source run {args.source_run_id}.", file=sys.stderr)
    return 1


def command_apply_patch_and_push(args: argparse.Namespace) -> int:
    profile = load_profile(args.profile_path)
    patch_root = Path(args.patch_root)
    patch_file = next(iter(sorted(patch_root.rglob("workflow-action-update-agent.patch"))), None)
    if patch_file is None:
        discovered_files = "\n".join(
            path.as_posix()
            for path in sorted(patch_root.rglob("*"))
            if path.is_file()
        )
        if discovered_files:
            raise RuntimeError(f"Patch file was not found under {patch_root}.\n{discovered_files}")
        raise RuntimeError(f"Patch file was not found under {patch_root}.")

    write_repair_metadata_files(
        profile=profile,
        source_run_id=args.source_run_id,
        source_run_url=args.source_run_url,
        source_workflow_name=args.source_workflow_name,
        repair_branch=args.repair_branch,
        target_branch="",
        ticket_id=args.ticket_id,
        body_file=args.body_file,
        pr_title_file=args.pr_title_file,
        commit_subject_file=args.commit_subject_file,
        commit_notes_file=args.commit_notes_file,
    )

    run_command(["git", "config", "user.name", "github-actions[bot]"])
    run_command(["git", "config", "user.email", "41898282+github-actions[bot]@users.noreply.github.com"])
    run_command(["git", "checkout", "-b", args.repair_branch])
    run_command(["git", "apply", "--index", str(patch_file)])

    commit_subject = Path(args.commit_subject_file).read_text(encoding="utf-8").strip()
    commit_notes = Path(args.commit_notes_file).read_text(encoding="utf-8").strip()
    commit_command = [
        "git",
        "commit",
        "-m",
        commit_subject,
        "-m",
        f"Task: {args.ticket_id}",
    ]
    if commit_notes:
        commit_command.extend(["-m", commit_notes])
    run_command(commit_command)
    run_command(["git", "push", "--set-upstream", "origin", args.repair_branch])

    head_sha = run_command(["git", "rev-parse", "HEAD"], capture_output=True).stdout.strip()
    write_outputs({"head_sha": head_sha}, args.github_output)
    return 0


def command_create_draft_pr(args: argparse.Namespace) -> int:
    profile = load_profile(args.profile_path)
    pr_title = Path(args.pr_title_file).read_text(encoding="utf-8").strip()
    pr_number = ""

    run_command(
        [
            "gh",
            "pr",
            "create",
            "--draft",
            "--base",
            args.target_branch,
            "--head",
            args.repair_branch,
            "--title",
            pr_title,
            "--body-file",
            args.body_file,
        ],
        capture_output=True,
    )
    pr_number = run_command(
        ["gh", "pr", "view", args.repair_branch, "--json", "number", "--jq", ".number"],
        capture_output=True,
    ).stdout.strip()

    label = profile_string(profile, "pr_trigger_label")
    if label:
        run_command(["gh", "pr", "edit", pr_number, "--add-label", label])

    write_outputs({"pr_number": pr_number}, args.github_output)
    return 0


def command_merge_pr(args: argparse.Namespace) -> int:
    run_command(["gh", "pr", "ready", args.pr_number])
    run_command(["gh", "pr", "merge", args.pr_number, "--merge", "--delete-branch"])
    return 0


def wait_for_workflow_run(
    repository: str,
    workflow_file: str,
    workflow_name: str,
    repair_branch: str,
    head_sha: str,
) -> str:
    deadline = time.time() + WAIT_TIMEOUT_SECONDS

    while time.time() < deadline:
        payload = parse_json_command(
            [
                "gh",
                "api",
                f"repos/{repository}/actions/workflows/{workflow_file}/runs?branch={repair_branch}&event=pull_request&per_page=20",
            ],
        )
        workflow_runs = payload.get("workflow_runs", []) if isinstance(payload, dict) else []
        candidates: list[tuple[datetime, str, str]] = []
        for run in workflow_runs:
            if not isinstance(run, dict):
                continue
            created_at = str(run.get("created_at") or "")
            if not created_at or str(run.get("head_sha") or "") != head_sha:
                continue
            candidates.append(
                (
                    parse_timestamp(created_at),
                    str(run.get("id") or ""),
                    str(run.get("conclusion") or ""),
                )
            )

        candidates.sort(reverse=True)
        if candidates:
            _, run_id, run_conclusion = candidates[0]
            if run_conclusion == "action_required":
                raise RuntimeError(
                    f"{workflow_name} run {run_id} for {repair_branch} is waiting for manual approval (conclusion: action_required).\n"
                    "Repository policy prevented unattended verification of the generated repair PR.",
                )
            print(f"Watching {workflow_name} run {run_id} for {repair_branch}")
            run_command(["gh", "run", "watch", run_id, "--repo", repository, "--exit-status"])
            return run_id

        time.sleep(15)

    raise RuntimeError(f"Timed out waiting for {workflow_name} on {repair_branch}")


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


def wait_for_review_state(
    *,
    pr_number: str,
    workflow_name: str,
    review_state_script: str,
    allowed_review_recommendations: list[str],
    expected_run_id: str,
    head_sha: str,
) -> None:
    deadline = time.time() + WAIT_TIMEOUT_SECONDS

    while time.time() < deadline:
        review_state = read_review_state(
            state_script=review_state_script,
            pr_number=pr_number,
        )
        observed_run_id = str(review_state.get("run_id") or "")
        observed_head_sha = str(review_state.get("head_sha") or "")
        recommendation = str(review_state.get("overall_recommendation") or "").strip().lower()

        if observed_run_id != expected_run_id:
            time.sleep(15)
            continue
        if observed_head_sha != head_sha:
            time.sleep(15)
            continue
        if recommendation in allowed_review_recommendations:
            print(
                f"Observed {workflow_name} recommendation {recommendation} from run {observed_run_id} for PR #{pr_number}"
            )
            return
        if recommendation:
            raise RuntimeError(
                f"{workflow_name} recommendation for PR #{pr_number} was '{recommendation}', "
                f"expected one of {', '.join(allowed_review_recommendations)}.",
            )

        time.sleep(15)

    raise RuntimeError(f"Timed out waiting for {workflow_name} recommendation on PR #{pr_number}")


def command_wait_for_pr_workflows(args: argparse.Namespace) -> int:
    profile = load_profile(args.profile_path)
    repository = os.environ["GITHUB_REPOSITORY"]
    for workflow in profile_validation_workflows(profile):
        run_id = wait_for_workflow_run(
            repository,
            str(workflow["workflow_file"]),
            str(workflow["workflow_name"]),
            args.repair_branch,
            args.head_sha,
        )
        review_state_script = str(workflow.get("review_state_script") or "")
        if review_state_script:
            wait_for_review_state(
                pr_number=args.pr_number,
                workflow_name=str(workflow["workflow_name"]),
                review_state_script=review_state_script,
                allowed_review_recommendations=[
                    str(recommendation).strip().lower()
                    for recommendation in workflow.get("allowed_review_recommendations", [])
                ],
                expected_run_id=run_id,
                head_sha=args.head_sha,
            )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Workflow Action Update Agent helper utility.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    resolve_inputs = subparsers.add_parser("resolve-inputs")
    resolve_inputs.add_argument("--profile-path", default=str(DEFAULT_PROFILE_PATH.relative_to(REPO_ROOT)))
    resolve_inputs.add_argument("--source-run-id", default="")
    resolve_inputs.add_argument("--target-branch", default="")
    resolve_inputs.add_argument("--ticket-id", required=True)
    resolve_inputs.add_argument("--current-ref-name", default="")
    resolve_inputs.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT", ""))
    resolve_inputs.set_defaults(func=command_resolve_inputs)

    collect_context = subparsers.add_parser("collect-context")
    collect_context.add_argument("--context-root", default=".codex/workflow-action-update-agent")
    collect_context.add_argument("--source-run-id", required=True)
    collect_context.add_argument("--source-run-url", required=True)
    collect_context.add_argument("--source-workflow-name", required=True)
    collect_context.set_defaults(func=command_collect_context)

    build_markdown = subparsers.add_parser("build-markdown")
    build_markdown.add_argument("--profile-path", default=str(DEFAULT_PROFILE_PATH.relative_to(REPO_ROOT)))
    build_markdown.add_argument("--context-root", default=".codex/workflow-action-update-agent")
    build_markdown.add_argument("--source-run-id", required=True)
    build_markdown.add_argument("--source-run-url", required=True)
    build_markdown.add_argument("--source-workflow-name", required=True)
    build_markdown.add_argument("--target-branch", required=True)
    build_markdown.add_argument("--repair-branch", required=True)
    build_markdown.add_argument("--ticket-id", required=True)
    build_markdown.set_defaults(func=command_build_markdown)

    package_patch = subparsers.add_parser("package-patch")
    package_patch.add_argument("--patch-file", required=True)
    package_patch.add_argument("--diffstat-file", required=True)
    package_patch.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT", ""))
    package_patch.set_defaults(func=command_package_patch)

    require_generated_patch = subparsers.add_parser("require-generated-patch")
    require_generated_patch.add_argument("--source-run-id", default="")
    require_generated_patch.set_defaults(func=command_require_generated_patch)

    apply_patch_and_push = subparsers.add_parser("apply-patch-and-push")
    apply_patch_and_push.add_argument("--profile-path", default=str(DEFAULT_PROFILE_PATH.relative_to(REPO_ROOT)))
    apply_patch_and_push.add_argument("--patch-root", required=True)
    apply_patch_and_push.add_argument("--repair-branch", required=True)
    apply_patch_and_push.add_argument("--source-run-id", required=True)
    apply_patch_and_push.add_argument("--source-run-url", required=True)
    apply_patch_and_push.add_argument("--source-workflow-name", required=True)
    apply_patch_and_push.add_argument("--ticket-id", required=True)
    apply_patch_and_push.add_argument("--body-file", required=True)
    apply_patch_and_push.add_argument("--pr-title-file", required=True)
    apply_patch_and_push.add_argument("--commit-subject-file", required=True)
    apply_patch_and_push.add_argument("--commit-notes-file", required=True)
    apply_patch_and_push.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT", ""))
    apply_patch_and_push.set_defaults(func=command_apply_patch_and_push)

    create_draft_pr = subparsers.add_parser("create-draft-pr")
    create_draft_pr.add_argument("--profile-path", default=str(DEFAULT_PROFILE_PATH.relative_to(REPO_ROOT)))
    create_draft_pr.add_argument("--body-file", required=True)
    create_draft_pr.add_argument("--pr-title-file", required=True)
    create_draft_pr.add_argument("--target-branch", required=True)
    create_draft_pr.add_argument("--repair-branch", required=True)
    create_draft_pr.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT", ""))
    create_draft_pr.set_defaults(func=command_create_draft_pr)

    merge_pr = subparsers.add_parser("merge-pr")
    merge_pr.add_argument("--pr-number", required=True)
    merge_pr.set_defaults(func=command_merge_pr)

    wait_for_workflows = subparsers.add_parser("wait-for-pr-workflows")
    wait_for_workflows.add_argument("--profile-path", default=str(DEFAULT_PROFILE_PATH.relative_to(REPO_ROOT)))
    wait_for_workflows.add_argument("--pr-number", required=True)
    wait_for_workflows.add_argument("--repair-branch", required=True)
    wait_for_workflows.add_argument("--head-sha", required=True)
    wait_for_workflows.set_defaults(func=command_wait_for_pr_workflows)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return int(args.func(args))
    except subprocess.CalledProcessError as exc:
        if exc.stdout:
            sys.stdout.write(exc.stdout)
        if exc.stderr:
            sys.stderr.write(exc.stderr)
        return exc.returncode
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError, re.error) as exc:
        print(str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
