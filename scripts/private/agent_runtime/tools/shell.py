#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import shlex
import subprocess

from ..runtime_context import AgentRunContext
from .paths import GIT_METADATA_DIR, GIT_METADATA_PREFIX, resolve_redirection_path

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
    ";",
)
SHELL_PIPE_SEPARATOR = "|"
SHELL_REDIRECT_STDIN = "<"
SHELL_REDIRECT_STDOUT = ">"
SHELL_REDIRECT_STDOUT_APPEND = ">>"


@dataclass(frozen=True)
class ParsedShellCommand:
    pipeline: list[list[str]]
    stdin_path: str | None = None
    stdout_path: str | None = None
    stdout_append: bool = False
    stderr_to_stdout: bool = False


@dataclass(frozen=True)
class ShellCommandResult:
    returncode: int
    stdout: str
    stderr: str


def split_shell_commands(command: str) -> list[ParsedShellCommand]:
    lexer = shlex.shlex(command.replace("\n", ";"), posix=True, punctuation_chars=";&|<>")
    lexer.whitespace_split = True
    tokens = list(lexer)
    commands: list[ParsedShellCommand] = []
    pipeline: list[list[str]] = []
    current: list[str] = []

    stdin_path: str | None = None
    stdout_path: str | None = None
    stdout_append = False
    stderr_to_stdout = False

    def append_pipeline_segment() -> None:
        nonlocal current
        if not current:
            raise ValueError("Unsupported empty command in agent shell pipeline.")
        pipeline.append(current)
        current = []

    def append_command() -> None:
        nonlocal pipeline, stdin_path, stdout_path, stdout_append, stderr_to_stdout
        if current:
            append_pipeline_segment()
        if pipeline:
            commands.append(
                ParsedShellCommand(
                    pipeline=pipeline,
                    stdin_path=stdin_path,
                    stdout_path=stdout_path,
                    stdout_append=stdout_append,
                    stderr_to_stdout=stderr_to_stdout,
                )
            )
            pipeline = []
            stdin_path = None
            stdout_path = None
            stdout_append = False
            stderr_to_stdout = False
            return
        if stdin_path or stdout_path or stderr_to_stdout:
            raise ValueError("Shell redirection requires a command in agent commands.")

    index = 0
    while index < len(tokens):
        word = tokens[index]
        if word in SHELL_COMMAND_SEPARATORS:
            append_command()
            index += 1
            continue
        if word == SHELL_PIPE_SEPARATOR:
            append_pipeline_segment()
            if index + 1 >= len(tokens) or tokens[index + 1] in {
                *SHELL_COMMAND_SEPARATORS,
                SHELL_PIPE_SEPARATOR,
            }:
                raise ValueError("Unsupported empty command in agent shell pipeline.")
            index += 1
            continue
        if word in {SHELL_REDIRECT_STDIN, SHELL_REDIRECT_STDOUT, SHELL_REDIRECT_STDOUT_APPEND}:
            if index + 1 >= len(tokens):
                raise ValueError(f"Missing path after shell redirection operator: {word}")
            target_path = tokens[index + 1]
            if any(character in target_path for character in ";&|<>"):
                raise ValueError(f"Unsupported shell redirection path in agent command: {target_path}")
            if word == SHELL_REDIRECT_STDIN:
                if stdin_path is not None:
                    raise ValueError("Multiple stdin redirections are not supported in agent commands.")
                stdin_path = target_path
            else:
                if stdout_path is not None:
                    raise ValueError("Multiple stdout redirections are not supported in agent commands.")
                stdout_path = target_path
                stdout_append = word == SHELL_REDIRECT_STDOUT_APPEND
            index += 2
            continue
        if word == "2":
            if tokens[index + 1:index + 3] == [">&", "1"]:
                stderr_to_stdout = True
                index += 3
                continue
            if tokens[index + 1:index + 4] == [">", "&", "1"]:
                stderr_to_stdout = True
                index += 4
                continue
        if any(character in word for character in ";&|<>"):
            raise ValueError(
                f"Unsupported shell syntax in agent command: {word}. "
                "Use simple commands, pipelines, or file redirection."
            )
        current.append(word)
        index += 1
    append_command()
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
    for parsed_command in split_shell_commands(command):
        for words in parsed_command.pipeline:
            lowered_words = [word.lower() for word in words]
            git_subcommand = find_subcommand(lowered_words, "git")
            if git_subcommand is not None and not is_allowed_git_command(lowered_words):
                raise ValueError(
                    f"Command is intentionally blocked for this agent step: git {git_subcommand}. "
                    "Use read-only git commands from the shell tool and leave repository mutation "
                    "to the patch tool or surrounding workflow."
                )
            gh_subcommand = find_subcommand(lowered_words, "gh")
            if gh_subcommand in FORBIDDEN_GH_SUBCOMMANDS:
                raise ValueError(
                    f"Command is intentionally blocked for this agent step: gh {gh_subcommand}. "
                    "Leave branch, commit, push, and PR lifecycle actions to the surrounding workflow."
                )


def format_parsed_shell_command(parsed_command: ParsedShellCommand) -> str:
    command_text = f" {SHELL_PIPE_SEPARATOR} ".join(
        shlex.join(words) for words in parsed_command.pipeline
    )
    if parsed_command.stdin_path:
        command_text = f"{command_text} {SHELL_REDIRECT_STDIN} {shlex.quote(parsed_command.stdin_path)}"
    if parsed_command.stdout_path:
        redirect = SHELL_REDIRECT_STDOUT_APPEND if parsed_command.stdout_append else SHELL_REDIRECT_STDOUT
        command_text = f"{command_text} {redirect} {shlex.quote(parsed_command.stdout_path)}"
    if parsed_command.stderr_to_stdout:
        command_text = f"{command_text} 2>&1"
    return command_text


def run_parsed_shell_command(parsed_command: ParsedShellCommand, context: AgentRunContext) -> ShellCommandResult:
    input_text: str | None = None
    if parsed_command.stdin_path:
        input_text = resolve_redirection_path(context, parsed_command.stdin_path).read_text(encoding="utf-8")
    output_path: Path | None = None
    if parsed_command.stdout_path:
        output_path = resolve_redirection_path(context, parsed_command.stdout_path)

    stdout_text = ""
    stderr_parts: list[str] = []
    exit_code = 0
    for words in parsed_command.pipeline:
        completed = subprocess.run(
            words,
            cwd=context.repo_root,
            input=input_text,
            text=True,
            capture_output=True,
            timeout=context.command_timeout,
            check=False,
        )
        exit_code = completed.returncode
        stdout_text = completed.stdout
        stderr_text = completed.stderr
        if parsed_command.stderr_to_stdout:
            stdout_text += stderr_text
            stderr_text = ""
        if stderr_text:
            stderr_parts.append(stderr_text.rstrip())
        input_text = stdout_text
        if completed.returncode != 0:
            break

    if output_path is not None:
        output_path.parent.mkdir(parents=True, exist_ok=True)
        mode = "a" if parsed_command.stdout_append else "w"
        with output_path.open(mode, encoding="utf-8") as output_file:
            output_file.write(stdout_text)
        stdout_text = ""

    return ShellCommandResult(
        returncode=exit_code,
        stdout=stdout_text,
        stderr="\n".join(stderr_parts),
    )


def reject_git_metadata_shell_arguments(parsed_command: ParsedShellCommand, context: AgentRunContext) -> None:
    for words in parsed_command.pipeline:
        for word in words:
            if not word or word.startswith("-") or "://" in word:
                continue
            try:
                path = context.resolve_repo_path(word)
                relative = path.relative_to(context.repo_root).as_posix()
            except ValueError:
                continue
            if relative == GIT_METADATA_DIR or relative.startswith(GIT_METADATA_PREFIX):
                raise ValueError(f"Command argument targets git metadata: {word}")
