#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from datetime import datetime, timedelta, timezone

from agent_runtime.github_actions import (
    dispatch_workflow_run,
    find_latest_workflow_run_candidate,
    poll_until,
    wait_for_dispatched_workflow_run,
    wait_for_workflow_run_completion,
)

from .runtime import (
    PULL_REQUEST_RUN_GRACE_SECONDS,
    STABILIZER_ENTRYPOINT_WORKFLOW_FILE,
    STABILIZER_ENTRYPOINT_WORKFLOW_NAME,
    WAIT_TIMEOUT_SECONDS,
    format_profile_template,
    run_command,
)


def build_validation_dispatch_context(
    *,
    pr_number: str,
    repair_branch: str,
    head_sha: str,
    target_branch: str,
    source_run_id: str,
    task_ref: str,
) -> dict[str, str]:
    return {
        "pr_number": pr_number,
        "repair_branch": repair_branch,
        "head_ref": repair_branch,
        "head_sha": head_sha,
        "target_branch": target_branch,
        "source_run_id": source_run_id,
        "task_ref": task_ref,
    }


def render_validation_workflow_dispatch_inputs(
    *,
    workflow: dict[str, object],
    dispatch_context: dict[str, str],
) -> dict[str, str]:
    raw_inputs = workflow.get("workflow_dispatch_inputs", {})
    if not isinstance(raw_inputs, dict):
        return {}
    return {
        str(name): format_profile_template(str(template), dispatch_context)
        for name, template in raw_inputs.items()
    }


def wait_for_existing_workflow_run(
    *,
    repository: str,
    workflow_file: str,
    repair_branch: str,
    head_sha: str,
    timeout_seconds: int,
) -> dict[str, str]:
    candidate = poll_until(
        lambda: find_latest_workflow_run_candidate(
            repository=repository,
            workflow_file=workflow_file,
            branch=repair_branch,
            head_sha=head_sha,
            events=["pull_request"],
        ),
        timeout_seconds=timeout_seconds,
    )
    return dict(candidate or {})


def dispatch_validation_workflow(
    *,
    repository: str,
    workflow_file: str,
    workflow_name: str,
    repair_branch: str,
    head_sha: str,
    workflow_inputs: dict[str, str],
) -> str:
    dispatched_after = datetime.now(timezone.utc) - timedelta(seconds=5)
    dispatch_workflow_run(
        workflow_file=workflow_file,
        ref=repair_branch,
        workflow_inputs=workflow_inputs,
    )
    candidate = poll_until(
        lambda: find_latest_workflow_run_candidate(
            repository=repository,
            workflow_file=workflow_file,
            branch=repair_branch,
            head_sha=head_sha,
            events=["workflow_dispatch"],
            created_after=dispatched_after,
        ),
        timeout_seconds=WAIT_TIMEOUT_SECONDS,
    )
    if candidate:
        run_id = str(candidate.get("id") or "")
        print(f"Watching {workflow_name} run {run_id} for {repair_branch}")
        wait_for_workflow_run_completion(
            repository=repository,
            workflow_name=workflow_name,
            run_id=run_id,
        )
        return run_id

    raise RuntimeError(f"Timed out waiting for dispatched {workflow_name} on {repair_branch}")


def ensure_validation_workflow_run(
    *,
    repository: str,
    workflow: dict[str, object],
    repair_branch: str,
    head_sha: str,
    dispatch_context: dict[str, str],
) -> tuple[str, str]:
    workflow_file = str(workflow.get("workflow_file") or "")
    workflow_name = str(workflow.get("workflow_name") or workflow_file)
    existing_run = wait_for_existing_workflow_run(
        repository=repository,
        workflow_file=workflow_file,
        repair_branch=repair_branch,
        head_sha=head_sha,
        timeout_seconds=PULL_REQUEST_RUN_GRACE_SECONDS,
    )
    if existing_run:
        run_id = str(existing_run.get("id") or "")
        print(f"Watching {workflow_name} run {run_id} for {repair_branch}")
        wait_for_workflow_run_completion(
            repository=repository,
            workflow_name=workflow_name,
            run_id=run_id,
        )
        return run_id, str(existing_run.get("event") or "pull_request")

    run_id = dispatch_validation_workflow(
        repository=repository,
        workflow_file=workflow_file,
        workflow_name=workflow_name,
        repair_branch=repair_branch,
        head_sha=head_sha,
        workflow_inputs=render_validation_workflow_dispatch_inputs(
            workflow=workflow,
            dispatch_context=dispatch_context,
        ),
    )
    return run_id, "workflow_dispatch"


def dispatch_stabilizer_workflow(
    *,
    repository: str,
    pr_number: str,
    head_sha: str,
    source_run_id: str,
    task_ref: str,
    profile_path: str,
    context_root: str,
    dispatch_ref: str,
    dispatch_nonce: str,
) -> str:
    dispatch_workflow_run(
        workflow_file=STABILIZER_ENTRYPOINT_WORKFLOW_FILE,
        ref=dispatch_ref,
        workflow_inputs={
            "pr_number": pr_number,
            "head_sha": head_sha,
            "source_run_id": source_run_id,
            "task_ref": task_ref,
            "profile_path": profile_path,
            "context_root": context_root,
            "dispatch_nonce": dispatch_nonce,
        },
    )
    run_id = wait_for_dispatched_workflow_run(
        repository=repository,
        workflow_file=STABILIZER_ENTRYPOINT_WORKFLOW_FILE,
        dispatch_nonce=dispatch_nonce,
        timeout_seconds=WAIT_TIMEOUT_SECONDS,
    )
    print(f"Watching {STABILIZER_ENTRYPOINT_WORKFLOW_NAME} run {run_id} for PR #{pr_number}")
    wait_for_workflow_run_completion(
        repository=repository,
        workflow_name=STABILIZER_ENTRYPOINT_WORKFLOW_NAME,
        run_id=run_id,
    )
    return run_id


def merge_pr(pr_number: str) -> None:
    run_command(["gh", "pr", "ready", pr_number], check=False)
    run_command(["gh", "pr", "merge", pr_number, "--merge", "--delete-branch"])
