#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from dataclasses import dataclass
import json
import subprocess
import sys
from typing import Any

from .contracts import AgentCommand
from .repo_tools import AgentRunContext, require_run_context
from .sdk_runtime import Agent, BaseModel, ConfigDict, Field, RunConfig, Runner, coerce_model_output
from .task_config import AgentTaskSettings


MAX_TASK_MANIFEST_PROMPT_HEAD_CHARS = 6000
MAX_TASK_MANIFEST_PROMPT_TAIL_CHARS = 3000


class TaskEstimate(BaseModel):
    model_config = ConfigDict(extra="forbid")

    fits: bool
    estimated_turns: int = Field(ge=1)
    confidence: float = Field(ge=0.0, le=1.0)
    reason: str = Field(min_length=1)
    split_recommendation: str | None = None


@dataclass(frozen=True)
class DiffStats:
    files: int = 0
    changed_lines: int = 0
    binary_files: int = 0
    error: str = ""


def compact_prompt_excerpt(prompt: str) -> str:
    if len(prompt) <= MAX_TASK_MANIFEST_PROMPT_HEAD_CHARS + MAX_TASK_MANIFEST_PROMPT_TAIL_CHARS:
        return prompt
    omitted_chars = len(prompt) - MAX_TASK_MANIFEST_PROMPT_HEAD_CHARS - MAX_TASK_MANIFEST_PROMPT_TAIL_CHARS
    return (
        prompt[:MAX_TASK_MANIFEST_PROMPT_HEAD_CHARS]
        + f"\n\n[omitted {omitted_chars} prompt characters]\n\n"
        + prompt[-MAX_TASK_MANIFEST_PROMPT_TAIL_CHARS:]
    )


def parse_prompt_context_value(prompt: str, label: str) -> str:
    prefix = f"- {label}: `"
    for line in prompt.splitlines():
        if line.startswith(prefix) and line.endswith("`"):
            return line[len(prefix):-1]
    return ""


def is_usable_prompt_ref(value: str) -> bool:
    return bool(value and not value.startswith("(") and "not provided" not in value)


def parse_numstat(output: str) -> DiffStats:
    files = 0
    changed_lines = 0
    binary_files = 0
    for line in output.splitlines():
        if not line.strip():
            continue
        parts = line.split("\t", 2)
        if len(parts) < 3:
            continue
        files += 1
        added, deleted = parts[0], parts[1]
        if added == "-" or deleted == "-":
            binary_files += 1
            continue
        changed_lines += int(added) + int(deleted)
    return DiffStats(files=files, changed_lines=changed_lines, binary_files=binary_files)


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


def collect_git_task_metrics(command: AgentCommand, prompt: str, context: AgentRunContext) -> dict[str, Any]:
    base_sha = parse_prompt_context_value(prompt, "Base SHA")
    head_sha = parse_prompt_context_value(prompt, "Head SHA")
    diff_scopes: dict[str, dict[str, Any]] = {}

    if is_usable_prompt_ref(base_sha) and is_usable_prompt_ref(head_sha) and base_sha != head_sha:
        diff_scopes["prompt_range"] = collect_numstat(context, [base_sha, head_sha]).__dict__
    diff_scopes["staged"] = collect_numstat(context, ["--cached"]).__dict__
    diff_scopes["unstaged"] = collect_numstat(context, []).__dict__

    total_files = 0
    total_changed_lines = 0
    total_binary_files = 0
    for stats in diff_scopes.values():
        if stats.get("error"):
            continue
        total_files += int(stats["files"])
        total_changed_lines += int(stats["changed_lines"])
        total_binary_files += int(stats["binary_files"])

    return {
        "base_sha": base_sha,
        "head_sha": head_sha,
        "diff_scopes": diff_scopes,
        "total_diff_files": total_files,
        "total_diff_changed_lines": total_changed_lines,
        "total_binary_files": total_binary_files,
        "diff_limits_apply": command is AgentCommand.REVIEW,
    }


def build_task_manifest(
    command: AgentCommand,
    prompt: str,
    settings: AgentTaskSettings,
    resolved_model: str,
) -> dict[str, Any]:
    context = require_run_context()
    prompt_lines = prompt.count("\n") + (1 if prompt else 0)
    return {
        "command": command.value,
        "agent_instance": settings.agent_instance.value,
        "model": resolved_model,
        "prompt_chars": len(prompt),
        "prompt_lines": prompt_lines,
        "prompt_excerpt": compact_prompt_excerpt(prompt),
        "limits": {
            "max_turns": settings.max_turns,
            "max_prompt_chars": settings.max_prompt_chars,
            "max_review_files": settings.max_review_files,
            "max_review_changed_lines": settings.max_review_changed_lines,
        },
        "git_metrics": collect_git_task_metrics(command, prompt, context),
    }


def deterministic_task_limit_violations(manifest: dict[str, Any]) -> list[str]:
    limits = manifest["limits"]
    git_metrics = manifest["git_metrics"]
    violations: list[str] = []

    prompt_chars = int(manifest["prompt_chars"])
    max_prompt_chars = int(limits["max_prompt_chars"])
    if prompt_chars > max_prompt_chars:
        violations.append(
            f"prompt has {prompt_chars} characters, above the {max_prompt_chars} character limit"
        )

    max_review_files = limits.get("max_review_files")
    if max_review_files is not None:
        total_diff_files = int(git_metrics["total_diff_files"])
        if total_diff_files > int(max_review_files):
            violations.append(
                f"review scope touches {total_diff_files} files, above the {max_review_files} file limit"
            )

    max_review_changed_lines = limits.get("max_review_changed_lines")
    if max_review_changed_lines is not None:
        total_changed_lines = int(git_metrics["total_diff_changed_lines"])
        if total_changed_lines > int(max_review_changed_lines):
            violations.append(
                "review scope changes "
                f"{total_changed_lines} lines, above the {max_review_changed_lines} line limit"
            )

    return violations


def task_estimator_instruction() -> str:
    return (
        "You are a preflight estimator for amp-dev-forge agent tasks. "
        "Decide whether the requested task can reasonably finish within the provided turn and size limits. "
        "Do not solve, review, or edit the task. Use the manifest metrics, prompt excerpt, and hard limits only. "
        "Set fits=false when the task is too broad, too large, ambiguous enough to require substantial exploration, "
        "or likely needs more turns than the configured limit. If splitting is needed, explain the smallest useful split."
    )


class TaskEstimatorAgent:
    agent_name = "OpenAI SDK Agent Task Estimator"

    def build_agent(self, resolved_model: str) -> Agent:
        return Agent(
            name=self.agent_name,
            instructions=task_estimator_instruction(),
            model=resolved_model,
            output_type=TaskEstimate,
        )

    async def run(
        self,
        manifest: dict[str, Any],
        settings: AgentTaskSettings,
        resolved_model: str,
    ) -> TaskEstimate:
        result = await Runner.run(
            self.build_agent(resolved_model),
            json.dumps(manifest, indent=2, sort_keys=True),
            max_turns=settings.task_estimate_turns,
            run_config=RunConfig(tracing_disabled=True),
        )
        return coerce_model_output(TaskEstimate, result.final_output)


def task_estimate_block_reasons(
    manifest: dict[str, Any],
) -> list[str]:
    return deterministic_task_limit_violations(manifest)


def task_estimate_advisory_reasons(
    manifest: dict[str, Any],
    estimate: TaskEstimate,
) -> list[str]:
    max_turns = int(manifest["limits"]["max_turns"])
    reasons: list[str] = []
    if estimate.estimated_turns > max_turns:
        reasons.append(
            f"estimator expects {estimate.estimated_turns} turns, above the {max_turns} turn limit"
        )
    if not estimate.fits:
        reasons.append(f"estimator marked task as not fitting: {estimate.reason}")
    return reasons


async def estimate_task_fit(
    command: AgentCommand,
    prompt: str,
    settings: AgentTaskSettings,
    resolved_model: str,
) -> None:
    manifest = build_task_manifest(command, prompt, settings, resolved_model)
    block_reasons = task_estimate_block_reasons(manifest)
    if block_reasons:
        raise RuntimeError(
            f"Agent task is too large for {command.value}: "
            + "; ".join(block_reasons)
            + ". Recommendation: Split the change or task into a smaller focused agent run."
        )

    try:
        estimate = await TaskEstimatorAgent().run(manifest, settings, resolved_model)
    except Exception as exc:  # noqa: BLE001
        print(f"Agent task estimator advisory unavailable: {exc}", file=sys.stderr)
        return

    advisory_reasons = task_estimate_advisory_reasons(manifest, estimate)
    if advisory_reasons:
        split = estimate.split_recommendation or "Split the change or task into a smaller focused agent run."
        print(
            f"Agent task estimator advisory for {command.value}: "
            + "; ".join(advisory_reasons)
            + f". Recommendation: {split}",
            file=sys.stderr,
        )
