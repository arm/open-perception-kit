#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import fnmatch
from pathlib import Path
import shlex
import subprocess

from .sdk_runtime import function_tool


MAX_TOOL_OUTPUT_CHARS = 24000
MAX_LIST_FILES = 400
READ_ONLY_GIT_SUBCOMMANDS = {
    "cat-file",
    "diff",
    "grep",
    "log",
    "ls-tree",
    "merge-base",
    "rev-parse",
    "show",
    "status",
}
FORBIDDEN_GIT_OPTIONS = {
    "--output",
}
FORBIDDEN_GH_SUBCOMMANDS = {
    "pr",
    "repo",
}
SHELL_COMMAND_SEPARATORS = (
    "&&",
    "||",
    ";",
)


class AgentRunContext:
    def __init__(self, repo_root: Path, command_timeout: int) -> None:
        self.repo_root = repo_root.resolve()
        self.command_timeout = command_timeout

    def resolve_repo_path(self, path_value: str) -> Path:
        path = Path(path_value)
        if path.is_absolute():
            resolved = path.resolve()
        else:
            resolved = (self.repo_root / path).resolve()
        if resolved != self.repo_root and self.repo_root not in resolved.parents:
            raise ValueError(f"Path escapes repository root: {path_value}")
        return resolved


RUN_CONTEXT: AgentRunContext | None = None


def set_run_context(repo_root: Path, command_timeout: int) -> AgentRunContext:
    global RUN_CONTEXT
    RUN_CONTEXT = AgentRunContext(repo_root, command_timeout)
    return RUN_CONTEXT


def require_run_context() -> AgentRunContext:
    if RUN_CONTEXT is None:
        raise RuntimeError("Agent run context has not been configured.")
    return RUN_CONTEXT


def truncate_tool_output(output: str) -> str:
    if len(output) <= MAX_TOOL_OUTPUT_CHARS:
        return output
    return (
        output[:MAX_TOOL_OUTPUT_CHARS]
        + f"\n\n[truncated {len(output) - MAX_TOOL_OUTPUT_CHARS} characters]\n"
    )


def split_shell_commands(command: str) -> list[list[str]]:
    lexer = shlex.shlex(command.replace("\n", ";"), posix=True, punctuation_chars=";&|<>")
    lexer.whitespace_split = True
    commands: list[list[str]] = []
    current: list[str] = []
    for word in lexer:
        if word in SHELL_COMMAND_SEPARATORS:
            if current:
                commands.append(current)
                current = []
            continue
        if any(character in word for character in ";&|<>"):
            raise ValueError(
                f"Unsupported shell syntax in agent command: {word}. "
                "Use simple commands separated by &&, ||, semicolon, or newline."
            )
        current.append(word)
    if current:
        commands.append(current)
    return commands


def find_subcommand(words: list[str], binary: str) -> str | None:
    if not words or Path(words[0]).name != binary:
        return None
    index = 1
    option_args = {"-C", "-c", "--git-dir", "--work-tree", "--namespace", "-R", "--repo"}
    while index < len(words):
        word = words[index]
        if word in option_args:
            index += 2
            continue
        if word.startswith("-"):
            index += 1
            continue
        return word
    return None


def has_forbidden_git_option(words: list[str]) -> bool:
    return any(
        word == option or word.startswith(f"{option}=")
        for word in words
        for option in FORBIDDEN_GIT_OPTIONS
    )


def is_allowed_git_command(words: list[str]) -> bool:
    git_subcommand = find_subcommand(words, "git")
    if git_subcommand is None:
        return True
    if has_forbidden_git_option(words):
        return False
    if git_subcommand == "apply":
        return "--check" in words
    return git_subcommand in READ_ONLY_GIT_SUBCOMMANDS


def reject_unsafe_shell_command(command: str) -> None:
    for words in split_shell_commands(command):
        words = [word.lower() for word in words]
        git_subcommand = find_subcommand(words, "git")
        if git_subcommand is not None and not is_allowed_git_command(words):
            raise ValueError(
                f"Command is intentionally blocked for this agent step: git {git_subcommand}. "
                "Use read-only git commands from the shell tool and leave repository mutation "
                "to the patch tool or surrounding workflow."
            )
        gh_subcommand = find_subcommand(words, "gh")
        if gh_subcommand in FORBIDDEN_GH_SUBCOMMANDS:
            raise ValueError(
                f"Command is intentionally blocked for this agent step: gh {gh_subcommand}. "
                "Leave branch, commit, push, and PR lifecycle actions to the surrounding workflow."
            )


@function_tool
def read_repo_file(path: str, start_line: int | None = None, end_line: int | None = None) -> str:
    """Read a UTF-8 text file from the checked-out repository."""

    context = require_run_context()
    file_path = context.resolve_repo_path(path)
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
        if ".git/" in relative or relative.startswith(".git/"):
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
    for words in split_shell_commands(command):
        completed = subprocess.run(
            words,
            cwd=context.repo_root,
            text=True,
            capture_output=True,
            timeout=context.command_timeout,
            check=False,
        )
        exit_code = completed.returncode
        output_parts.extend(
            [
                f"$ {shlex.join(words)}",
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
    completed = subprocess.run(
        ["git", "apply", "--whitespace=nowarn"],
        cwd=context.repo_root,
        input=patch,
        text=True,
        capture_output=True,
        timeout=context.command_timeout,
        check=False,
    )
    output_parts = [
        f"exit_code={completed.returncode}",
        "--- stdout ---",
        completed.stdout.rstrip(),
        "--- stderr ---",
        completed.stderr.rstrip(),
    ]
    return truncate_tool_output("\n".join(output_parts).rstrip() + "\n")
