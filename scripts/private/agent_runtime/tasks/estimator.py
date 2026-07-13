#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from dataclasses import dataclass
import subprocess
import sys
from typing import Any

from ..config.task import AgentTaskSettings
from ..contracts import AgentCommand
from ..runtime_context import AgentRunContext, require_run_context


@dataclass(frozen=True)
class DiffStats:
    files: int = 0
    changed_lines: int = 0
    error: str = ""


def parse_numstat(output: str) -> DiffStats:
    files = 0
    changed_lines = 0
    for line in output.splitlines():
        if not line.strip():
            continue
        parts = line.split("\t", 2)
        if len(parts) < 3:
            continue
        files += 1
        added, deleted = parts[0], parts[1]
        if added == "-" or deleted == "-":
            continue
        changed_lines += int(added) + int(deleted)
    return DiffStats(files=files, changed_lines=changed_lines)


def collect_numstat(context: AgentRunContext, diff_args: list[str]) -> DiffStats:
    completed = subprocess.run(
        ["git", "diff", "--numstat", *diff_args],
        cwd=context.repo_root,
        text=True,
        capture_output=True,
        timeout=context.command_timeout,
        check=False,
    )
    if completed.returncode != 0:
        return DiffStats(error=(completed.stderr or completed.stdout).strip())
    return parse_numstat(completed.stdout)


def collect_git_task_metrics(
    context: AgentRunContext,
    *,
    base_sha: str = "",
    head_sha: str = "",
) -> dict[str, Any]:
    diff_stats: list[DiffStats] = []

    if base_sha and head_sha and base_sha != head_sha:
        diff_stats.append(collect_numstat(context, [base_sha, head_sha]))
    diff_stats.append(collect_numstat(context, ["--cached"]))
    diff_stats.append(collect_numstat(context, []))

    total_files = 0
    total_changed_lines = 0
    for stats in diff_stats:
        if stats.error:
            continue
        total_files += stats.files
        total_changed_lines += stats.changed_lines

    return {
        "total_diff_files": total_files,
        "total_diff_changed_lines": total_changed_lines,
    }


def build_task_manifest(
    prompt: str,
    settings: AgentTaskSettings,
    *,
    base_sha: str = "",
    head_sha: str = "",
) -> dict[str, Any]:
    context = require_run_context()
    return {
        "prompt_chars": len(prompt),
        "limits": {
            "max_prompt_chars": settings.max_prompt_chars,
            "max_review_files": settings.max_review_files,
            "max_review_changed_lines": settings.max_review_changed_lines,
        },
        "git_metrics": collect_git_task_metrics(
            context,
            base_sha=base_sha,
            head_sha=head_sha,
        ),
    }


def deterministic_task_limit_violations(manifest: dict[str, Any]) -> list[str]:
    limits = manifest["limits"]
    violations: list[str] = []

    prompt_chars = int(manifest["prompt_chars"])
    max_prompt_chars = limits["max_prompt_chars"]
    if max_prompt_chars is not None and prompt_chars > int(max_prompt_chars):
        violations.append(
            f"prompt has {prompt_chars} characters, above the {max_prompt_chars} character limit"
        )

    return violations


def deterministic_task_advisory_reasons(manifest: dict[str, Any]) -> list[str]:
    limits = manifest["limits"]
    git_metrics = manifest["git_metrics"]
    reasons: list[str] = []

    max_review_files = limits.get("max_review_files")
    if max_review_files is not None:
        total_diff_files = int(git_metrics["total_diff_files"])
        if total_diff_files > int(max_review_files):
            reasons.append(
                f"review scope touches {total_diff_files} files, above the {max_review_files} file limit"
            )

    max_review_changed_lines = limits.get("max_review_changed_lines")
    if max_review_changed_lines is not None:
        total_changed_lines = int(git_metrics["total_diff_changed_lines"])
        if total_changed_lines > int(max_review_changed_lines):
            reasons.append(
                "review scope changes "
                f"{total_changed_lines} lines, above the {max_review_changed_lines} line limit"
            )

    return reasons


def task_estimate_block_reasons(
    manifest: dict[str, Any],
) -> list[str]:
    return deterministic_task_limit_violations(manifest)


def task_estimate_advisory_reasons(
    manifest: dict[str, Any],
) -> list[str]:
    return deterministic_task_advisory_reasons(manifest)


async def estimate_task_fit(
    command: AgentCommand,
    prompt: str,
    settings: AgentTaskSettings,
    *,
    base_sha: str = "",
    head_sha: str = "",
) -> None:
    manifest = build_task_manifest(
        prompt,
        settings,
        base_sha=base_sha,
        head_sha=head_sha,
    )
    block_reasons = task_estimate_block_reasons(manifest)
    if block_reasons:
        raise RuntimeError(
            f"Agent task is too large for {command.value}: "
            + "; ".join(block_reasons)
            + ". Recommendation: Split the change or task into a smaller focused agent run."
        )

    advisory_reasons = task_estimate_advisory_reasons(manifest)
    if advisory_reasons:
        print(
            f"Agent task size advisory for {command.value}: "
            + "; ".join(advisory_reasons)
            + ". Recommendation: Continue the run, but keep the task focused and call out split points.",
            file=sys.stderr,
        )
