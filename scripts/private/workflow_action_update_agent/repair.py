#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import os
import re
import shutil
import sys
import textwrap
from pathlib import Path

from agent_runtime.github_actions import (
    authorized_source_pr_number,
    download_workflow_run_artifacts,
    read_workflow_run,
    source_pull_request_numbers,
    write_workflow_run_log_file,
)
from agent_runtime.contracts import AgentInstance

from .runtime import (
    CONTEXT_ROOT_TOKEN,
    PR_TEMPLATE_PATH,
    PR_AUTOMATION_END,
    PR_AUTOMATION_START,
    PR_DESCRIPTION_END,
    PR_DESCRIPTION_START,
    REPAIR_BRANCH_TOKEN,
    REPAIR_AUTHORIZATION_LABEL_TOKEN,
    REPAIR_DEFINITION_OF_DONE_TOKEN,
    SOURCE_PR_NUMBER_TOKEN,
    SOURCE_RUN_ID_TOKEN,
    SOURCE_RUN_URL_TOKEN,
    SOURCE_WORKFLOW_NAME_TOKEN,
    TARGET_BRANCH_TOKEN,
    TASK_REF_TOKEN,
    VALIDATION_COMMANDS_TOKEN,
    format_profile_template,
    load_markdown_template,
    load_profile,
    profile_agent_model,
    profile_bool,
    profile_prompt_replacements,
    profile_string,
    profile_string_list,
    render_markdown_template,
    resolve_task_ref,
    run_command,
    write_json_file,
    write_outputs,
)


def render_repair_ci_badge(repair_branch: str) -> str:
    return (
        "[![Perception Experience Kit CI Pipeline]"
        "(https://github.com/Arm-Debug/amp-dev-forge/actions/workflows/pek-ci.yml/badge.svg"
        f"?branch={repair_branch})]"
        "(https://github.com/Arm-Debug/amp-dev-forge/actions/workflows/pek-ci.yml)"
    )


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
    source_pr_number: str = "",
    source_run_url: str,
    source_workflow_name: str,
    repair_branch: str,
    target_branch: str,
    task_ref: str,
) -> dict[str, str]:
    context = {
        "automation_name": profile_string(profile, "automation_name"),
        "source_run_id": source_run_id,
        "source_pr_number": source_pr_number or "n/a",
        "source_run_url": source_run_url,
        "source_workflow_name": source_workflow_name,
        "repair_branch": repair_branch,
        "target_branch": target_branch,
        "task_ref": task_ref,
        "repair_authorization_label": profile_string(profile, "repair_authorization_label"),
        "pr_trigger_label": profile_string(profile, "pr_trigger_label"),
    }
    context["repair_definition_of_done"] = "\n".join(
        f"- {format_profile_template(item, context)}"
        for item in profile_string_list(profile, "repair_definition_of_done")
    )
    return context


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
    source_pr_number: str = "",
    source_run_url: str,
    source_workflow_name: str,
    repair_branch: str,
    target_branch: str = "",
    task_ref: str = "",
) -> tuple[str, str, str, str]:
    context = build_profile_context(
        profile,
        source_run_id=source_run_id,
        source_pr_number=source_pr_number,
        source_run_url=source_run_url,
        source_workflow_name=source_workflow_name,
        repair_branch=repair_branch,
        target_branch=target_branch,
        task_ref=task_ref,
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
    source_pr_number: str = "",
    source_run_url: str,
    source_workflow_name: str,
    repair_branch: str,
    target_branch: str,
    task_ref: str,
    body_file: str,
    pr_title_file: str,
    commit_subject_file: str,
    commit_notes_file: str,
) -> None:
    body, pr_title, commit_subject, commit_notes = render_repair_metadata_values(
        profile=profile,
        source_run_id=source_run_id,
        source_pr_number=source_pr_number,
        source_run_url=source_run_url,
        source_workflow_name=source_workflow_name,
        repair_branch=repair_branch,
        target_branch=target_branch,
        task_ref=task_ref,
    )
    Path(body_file).write_text(body, encoding="utf-8")
    Path(pr_title_file).write_text(pr_title, encoding="utf-8")
    Path(commit_subject_file).write_text(commit_subject, encoding="utf-8")
    Path(commit_notes_file).write_text(commit_notes, encoding="utf-8")


def build_markdown_documents(
    *,
    profile: dict[str, object],
    context_root: Path,
    source_run_id: str,
    source_pr_number: str,
    source_run_url: str,
    source_workflow_name: str,
    target_branch: str,
    repair_branch: str,
    task_ref: str,
    artifact_files: list[str],
) -> dict[str, str]:
    file_inventory = "# File Inventory\n\n" + "\n".join(f"- `{item}`" for item in artifact_files)
    prompt_replacements = profile_prompt_replacements(profile)
    profile_context = build_profile_context(
        profile,
        source_run_id=source_run_id,
        source_pr_number=source_pr_number,
        source_run_url=source_run_url,
        source_workflow_name=source_workflow_name,
        repair_branch=repair_branch,
        target_branch=target_branch,
        task_ref=task_ref,
    )

    return {
        "file-inventory.md": file_inventory,
        "failure-context.md": render_markdown_template(
            "failure-context.md.in",
            {
                CONTEXT_ROOT_TOKEN: context_root.as_posix(),
                REPAIR_BRANCH_TOKEN: repair_branch,
                REPAIR_AUTHORIZATION_LABEL_TOKEN: profile_string(profile, "repair_authorization_label"),
                SOURCE_PR_NUMBER_TOKEN: source_pr_number or "n/a",
                SOURCE_RUN_ID_TOKEN: source_run_id,
                SOURCE_RUN_URL_TOKEN: source_run_url,
                SOURCE_WORKFLOW_NAME_TOKEN: source_workflow_name,
                TARGET_BRANCH_TOKEN: target_branch,
                TASK_REF_TOKEN: task_ref,
            },
        ),
        "minimal-change-policy.md": load_markdown_template("minimal-change-policy.md"),
        "constraints.md": load_markdown_template("constraints.md"),
        "validation.md": render_markdown_template(
            "validation.md.in",
            {VALIDATION_COMMANDS_TOKEN: prompt_replacements[VALIDATION_COMMANDS_TOKEN]},
        ),
        "goal.md": render_markdown_template(
            "repair-goal.md.in",
            {
                **prompt_replacements,
                REPAIR_DEFINITION_OF_DONE_TOKEN: profile_context["repair_definition_of_done"],
            },
        ),
    }


def command_resolve_inputs(args: argparse.Namespace) -> int:
    profile = load_profile(args.profile_path)
    task_ref = ""

    repository = os.environ["GITHUB_REPOSITORY"]
    current_ref_name = args.current_ref_name or "main"
    should_run = True
    skip_reason = ""
    source_run_id = args.source_run_id
    source_pr_number = ""
    source_run_url = ""
    source_workflow_name = ""
    target_branch = args.target_branch or current_ref_name
    repair_branch = ""

    if not source_run_id:
        should_run = False
        skip_reason = "No source run ID was provided."
    else:
        run_json = read_workflow_run(repository=repository, run_id=source_run_id)
        if not isinstance(run_json, dict):
            raise RuntimeError(f"Unexpected workflow run payload for source run {source_run_id}.")

        source_run_url = str(run_json.get("html_url") or "")
        source_workflow_name = str(run_json.get("name") or "")
        source_workflow_conclusion = str(run_json.get("conclusion") or "")
        source_head_branch = str(run_json.get("head_branch") or "")
        source_head_repository = str(dict(run_json.get("head_repository") or {}).get("full_name") or "")
        source_pr_numbers = source_pull_request_numbers(run_json)
        target_branch = args.target_branch or source_head_branch or current_ref_name

        repair_guard = re.compile(profile_string(profile, "repair_branch_guard_regex"))
        require_failure_conclusion = profile_bool(profile, "require_failure_conclusion", True)
        authorization_label = profile_string(profile, "repair_authorization_label")
        if require_failure_conclusion and source_workflow_conclusion != "failure":
            should_run = False
            skip_reason = f"Source workflow conclusion is '{source_workflow_conclusion}'."
        elif source_head_repository and source_head_repository != repository:
            should_run = False
            skip_reason = f"Source workflow head repository '{source_head_repository}' is not trusted."
        elif repair_guard.match(source_head_branch or ""):
            should_run = False
            skip_reason = "Source workflow already runs on a repair branch."
        elif not source_pr_numbers:
            should_run = False
            skip_reason = "Source workflow run is not associated with a pull request."
        else:
            source_pr_number = authorized_source_pr_number(
                repository=repository,
                source_pr_numbers=source_pr_numbers,
                authorization_label=authorization_label,
            )
            if not source_pr_number:
                should_run = False
                skip_reason = (
                    "Source pull request is missing required "
                    f"'{authorization_label}' label for external repair PR creation."
                )
            else:
                task_ref = resolve_task_ref(
                    args.task_ref,
                    target_branch,
                    source_head_branch,
                    current_ref_name,
                    purpose="Repair branch creation",
                )
                repair_branch = format_profile_template(
                    profile_string(profile, "repair_branch_template"),
                    build_profile_context(
                        profile,
                        source_run_id=source_run_id,
                        source_pr_number=source_pr_number,
                        source_run_url=source_run_url,
                        source_workflow_name=source_workflow_name,
                        repair_branch="",
                        target_branch=target_branch,
                        task_ref=task_ref,
                    ),
                )

    write_outputs(
        {
            "should_run": "true" if should_run else "false",
            "skip_reason": skip_reason,
            "source_run_id": source_run_id or "",
            "source_pr_number": source_pr_number,
            "source_run_url": source_run_url,
            "source_workflow_name": source_workflow_name,
            "target_branch": target_branch,
            "task_ref": task_ref,
            "repair_branch": repair_branch,
            "agent_model": profile_agent_model(profile, AgentInstance.REPAIR, args.profile_path),
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
    run_json = read_workflow_run(repository=repository, run_id=args.source_run_id)
    write_json_file(context_root / "source-run.json", run_json)
    write_workflow_run_log_file(
        repository=repository,
        run_id=args.source_run_id,
        output_path=context_root / "source-run.log",
        fallback_url=args.source_run_url,
    )
    download_workflow_run_artifacts(
        repository=repository,
        run_id=args.source_run_id,
        artifact_root=artifact_root,
    )
    return 0


def command_build_markdown(args: argparse.Namespace) -> int:
    profile = load_profile(args.profile_path)
    task_ref = resolve_task_ref(args.task_ref, purpose="Repair prompt generation")
    context_root = Path(args.context_root)
    artifact_files = sorted(
        path.relative_to(context_root).as_posix()
        for path in context_root.rglob("*")
        if path.is_file()
    )

    documents = build_markdown_documents(
        profile=profile,
        context_root=context_root,
        source_run_id=args.source_run_id,
        source_pr_number=getattr(args, "source_pr_number", ""),
        source_run_url=args.source_run_url,
        source_workflow_name=args.source_workflow_name,
        target_branch=args.target_branch,
        repair_branch=args.repair_branch,
        task_ref=task_ref,
        artifact_files=artifact_files,
    )

    for name, content in documents.items():
        (context_root / name).write_text(
            textwrap.dedent(content).strip() + "\n",
            encoding="utf-8",
        )
    return 0


def command_package_repository_changes(args: argparse.Namespace) -> int:
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


def command_require_generated_changes(args: argparse.Namespace) -> int:
    print(f"Agent did not produce repository changes for source run {args.source_run_id}.", file=sys.stderr)
    return 1


def command_apply_repair_changes_and_push(args: argparse.Namespace) -> int:
    profile = load_profile(args.profile_path)
    task_ref = resolve_task_ref(args.task_ref, purpose="Repair commit creation")
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
        source_pr_number=getattr(args, "source_pr_number", ""),
        source_run_url=args.source_run_url,
        source_workflow_name=args.source_workflow_name,
        repair_branch=args.repair_branch,
        target_branch=args.target_branch,
        task_ref=task_ref,
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
        f"Task: {task_ref}",
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
