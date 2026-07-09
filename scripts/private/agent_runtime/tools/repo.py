#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import fnmatch
import subprocess

from ..runtime_context import require_run_context
from ..sdk_runtime import function_tool
from .paths import is_git_metadata_path, resolve_safe_repo_path, validate_patch_paths
from .shell import (
    format_parsed_shell_command,
    reject_git_metadata_shell_arguments,
    reject_unsafe_shell_command,
    run_parsed_shell_command,
    split_shell_commands,
)

MAX_TOOL_OUTPUT_CHARS = 24000
MAX_LIST_FILES = 400


def truncate_tool_output(output: str) -> str:
    if len(output) <= MAX_TOOL_OUTPUT_CHARS:
        return output
    return (
        output[:MAX_TOOL_OUTPUT_CHARS]
        + f"\n\n[truncated {len(output) - MAX_TOOL_OUTPUT_CHARS} characters]\n"
    )


@function_tool
def read_repo_file(path: str, start_line: int | None = None, end_line: int | None = None) -> str:
    """Read a UTF-8 text file from the checked-out repository."""

    context = require_run_context()
    file_path = resolve_safe_repo_path(context, path, "Read")
    lines = file_path.read_text(encoding="utf-8").splitlines()
    first = max((start_line or 1) - 1, 0)
    last = end_line if end_line is not None else len(lines)
    numbered = [
        f"{line_number}: {line}"
        for line_number, line in enumerate(lines[first:last], start=first + 1)
    ]
    return truncate_tool_output("\n".join(numbered))


@function_tool
def list_repo_files(pattern: str = "**/*") -> str:
    """List repository files matching a glob pattern."""

    context = require_run_context()
    matches: list[str] = []
    for path in context.repo_root.rglob("*"):
        if not path.is_file():
            continue
        relative = path.relative_to(context.repo_root).as_posix()
        if is_git_metadata_path(relative):
            continue
        try:
            resolved_relative = path.resolve().relative_to(context.repo_root).as_posix()
        except ValueError:
            continue
        if is_git_metadata_path(resolved_relative):
            continue
        if pattern == "**/*" or fnmatch.fnmatch(relative, pattern):
            matches.append(relative)
        if len(matches) >= MAX_LIST_FILES:
            matches.append(f"[truncated after {MAX_LIST_FILES} files]")
            break
    return "\n".join(sorted(matches))


@function_tool
def run_shell_command(command: str) -> str:
    """Run a read, build, or validation shell command in the repository root."""

    context = require_run_context()
    reject_unsafe_shell_command(command)
    output_parts: list[str] = []
    exit_code = 0
    for parsed_command in split_shell_commands(command):
        reject_git_metadata_shell_arguments(parsed_command, context)
        completed = run_parsed_shell_command(parsed_command, context)
        exit_code = completed.returncode
        output_parts.extend(
            [
                f"$ {format_parsed_shell_command(parsed_command)}",
                f"exit_code={completed.returncode}",
                "--- stdout ---",
                completed.stdout.rstrip(),
                "--- stderr ---",
                completed.stderr.rstrip(),
            ]
        )
        if completed.returncode != 0:
            break
    output_parts.insert(0, f"exit_code={exit_code}")
    return truncate_tool_output("\n".join(output_parts).rstrip() + "\n")


@function_tool
def apply_unified_diff(patch: str) -> str:
    """Apply a unified git diff patch to the repository working tree."""

    context = require_run_context()
    validate_patch_paths(context, patch)
    completed = subprocess.run(
        ["git", "apply", "--whitespace=nowarn"],
        cwd=context.repo_root,
        input=patch,
        text=True,
        capture_output=True,
        timeout=context.command_timeout,
        check=False,
    )
    if completed.returncode != 0:
        return truncate_tool_output(
            "\n".join(
                [
                    "Patch apply failed.",
                    "--- stdout ---",
                    completed.stdout.rstrip(),
                    "--- stderr ---",
                    completed.stderr.rstrip(),
                ]
            ).rstrip()
            + "\n"
        )
    return "exit_code=0\nPatch applied.\n"
