#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
import shlex
import subprocess

from ..review.context import ReviewRunContext
from ..runtime_context import AgentRunContext
from .paths import is_git_metadata_path, resolve_stdin_redirection_path

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
    "-C",
    "-c",
    "--exec-path",
    "--git-dir",
    "--namespace",
    "--output",
    "--super-prefix",
    "--work-tree",
}
FORBIDDEN_GH_SUBCOMMANDS = {
    "pr",
    "repo",
}
BINARY_OPTIONS_WITH_VALUES = {
    "git": FORBIDDEN_GIT_OPTIONS,
    "gh": {
        "-R",
        "--hostname",
        "--repo",
    },
}
SHELL_COMMAND_SEPARATORS = (
    "&&",
    ";",
)
SHELL_PIPE_SEPARATOR = "|"
SHELL_REDIRECT_STDIN = "<"
SHELL_UNSUPPORTED_STDOUT_REDIRECTS = (">", ">>")
SENSITIVE_REVIEW_ENV_PREFIXES = ("ACTIONS_", "GH_", "GITHUB_", "OPENAI_")
SENSITIVE_REVIEW_ENV_MARKERS = (
    "ACCESS_KEY",
    "API_KEY",
    "AUTH",
    "COOKIE",
    "CREDENTIAL",
    "JWT",
    "PASSWORD",
    "PRIVATE_KEY",
    "SECRET",
    "SESSION",
    "TOKEN",
)
SENSITIVE_REVIEW_ENV_NAMES = {
    "DOCKER_CONFIG",
    "GIT_ASKPASS",
    "GIT_SSH_COMMAND",
    "SSH_ASKPASS",
    "SSH_AUTH_SOCK",
}
SAFE_REVIEW_ENV_NAMES = {
    "AR",
    "AS",
    "CC",
    "CFLAGS",
    "CI",
    "CMAKE_GENERATOR",
    "CMAKE_PREFIX_PATH",
    "CMAKE_TOOLCHAIN_FILE",
    "CPP",
    "CPPFLAGS",
    "CROSS_COMPILE",
    "CXX",
    "CXXFLAGS",
    "DEVELOPER_DIR",
    "DYLD_LIBRARY_PATH",
    "LANG",
    "LC_ALL",
    "LC_CTYPE",
    "LD",
    "LDFLAGS",
    "LD_LIBRARY_PATH",
    "LIBRARY_PATH",
    "MACOSX_DEPLOYMENT_TARGET",
    "MAKEFLAGS",
    "NINJA_STATUS",
    "NM",
    "NODE_PATH",
    "OBJCOPY",
    "OBJDUMP",
    "PATH",
    "PKG_CONFIG_PATH",
    "PYTHONHOME",
    "PYTHONPATH",
    "PYTHONUNBUFFERED",
    "QEMU_LD_PREFIX",
    "RANLIB",
    "REQUESTS_CA_BUNDLE",
    "SDKROOT",
    "SHELL",
    "SSL_CERT_DIR",
    "SSL_CERT_FILE",
    "STRIP",
    "SYSROOT",
    "TEMP",
    "TERM",
    "TMP",
    "TMPDIR",
    "TZ",
    "VCPKG_ROOT",
    "VIRTUAL_ENV",
}
SAFE_REVIEW_ENV_PREFIXES = ("CMAKE_", "LC_", "MESON_", "NINJA_", "PEK_")


@dataclass(frozen=True)
class ParsedShellCommand:
    pipeline: list[list[str]]
    stdin_path: str | None = None
    stderr_to_stdout: bool = False


@dataclass(frozen=True)
class ShellCommandResult:
    returncode: int
    stdout: str
    stderr: str


def is_sensitive_review_environment_name(name: str) -> bool:
    normalized = name.upper()
    return (
        normalized in SENSITIVE_REVIEW_ENV_NAMES
        or normalized.startswith(SENSITIVE_REVIEW_ENV_PREFIXES)
        or normalized.endswith("_KEY")
        or any(marker in normalized for marker in SENSITIVE_REVIEW_ENV_MARKERS)
    )


def is_safe_review_environment_name(name: str) -> bool:
    normalized = name.upper()
    return not is_sensitive_review_environment_name(normalized) and (
        normalized in SAFE_REVIEW_ENV_NAMES
        or normalized.startswith(SAFE_REVIEW_ENV_PREFIXES)
    )


def build_subprocess_environment(context: AgentRunContext) -> dict[str, str]:
    if not isinstance(context, ReviewRunContext):
        return dict(os.environ)
    environment = {
        name: value
        for name, value in os.environ.items()
        if is_safe_review_environment_name(name)
    }
    isolated_home = context.repo_root / ".agent-runtime/review-shell-home"
    environment["HOME"] = str(isolated_home)
    environment["PWD"] = str(context.repo_root)
    environment["XDG_CACHE_HOME"] = str(isolated_home / "cache")
    environment["XDG_CONFIG_HOME"] = str(isolated_home / "config")
    environment["XDG_DATA_HOME"] = str(isolated_home / "data")
    environment["GH_CONFIG_DIR"] = str(isolated_home / "gh")
    return environment


def split_shell_commands(command: str) -> list[ParsedShellCommand]:
    lexer = shlex.shlex(command.replace("\n", ";"), posix=True, punctuation_chars=";&|<>")
    lexer.whitespace_split = True
    tokens = list(lexer)
    commands: list[ParsedShellCommand] = []
    pipeline: list[list[str]] = []
    current: list[str] = []

    stdin_path: str | None = None
    stderr_to_stdout = False

    def append_pipeline_segment() -> None:
        nonlocal current
        if not current:
            raise ValueError("Unsupported empty command in agent shell pipeline.")
        pipeline.append(current)
        current = []

    def append_command() -> None:
        nonlocal pipeline, stdin_path, stderr_to_stdout
        if current:
            append_pipeline_segment()
        if pipeline:
            if stderr_to_stdout and len(pipeline) > 1:
                raise ValueError("stderr redirection with pipelines is not supported in agent commands.")
            commands.append(
                ParsedShellCommand(
                    pipeline=pipeline,
                    stdin_path=stdin_path,
                    stderr_to_stdout=stderr_to_stdout,
                )
            )
            pipeline = []
            stdin_path = None
            stderr_to_stdout = False
            return
        if stdin_path or stderr_to_stdout:
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
        if word in SHELL_UNSUPPORTED_STDOUT_REDIRECTS:
            raise ValueError(
                "stdout redirection is not supported in agent shell commands. "
                "Use apply_unified_diff for repository changes."
            )
        if word == SHELL_REDIRECT_STDIN:
            if index + 1 >= len(tokens):
                raise ValueError(f"Missing path after shell redirection operator: {word}")
            target_path = tokens[index + 1]
            if any(character in target_path for character in ";&|<>"):
                raise ValueError(f"Unsupported shell redirection path in agent command: {target_path}")
            if stdin_path is not None:
                raise ValueError("Multiple stdin redirections are not supported in agent commands.")
            stdin_path = target_path
            index += 2
            continue
        if word == "2" and index + 1 < len(tokens):
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
                "Use simple commands, pipelines, or stdin file redirection."
            )
        current.append(word)
        index += 1
    append_command()
    return commands


def option_has_inline_value(word: str, option: str) -> bool:
    if option.startswith("--"):
        return word.startswith(f"{option}=")
    return word.startswith(option) and word != option


def find_subcommand(words: list[str], binary: str) -> str | None:
    if not words or Path(words[0]).name != binary:
        return None
    index = 1
    options_with_values = BINARY_OPTIONS_WITH_VALUES.get(binary, set())
    while index < len(words):
        word = words[index]
        if word == "--":
            index += 1
            continue
        if word in options_with_values:
            index += 2
            continue
        if any(option_has_inline_value(word, option) for option in options_with_values):
            index += 1
            continue
        if word.startswith("-"):
            index += 1
            continue
        return word
    return None


def has_forbidden_git_option(words: list[str]) -> bool:
    return any(
        word == option
        or word.startswith(f"{option}=")
        or (not option.startswith("--") and word.startswith(option) and word != option)
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
    command_segments = []
    for index, words in enumerate(parsed_command.pipeline):
        command_segment = shlex.join(words)
        if index == 0 and parsed_command.stdin_path:
            command_segment = f"{command_segment} {SHELL_REDIRECT_STDIN} {shlex.quote(parsed_command.stdin_path)}"
        command_segments.append(command_segment)
    command_text = f" {SHELL_PIPE_SEPARATOR} ".join(command_segments)
    if parsed_command.stderr_to_stdout:
        command_text = f"{command_text} 2>&1"
    return command_text


def run_parsed_shell_command(parsed_command: ParsedShellCommand, context: AgentRunContext) -> ShellCommandResult:
    stage_input: str | None = None
    if parsed_command.stdin_path:
        stage_input = resolve_stdin_redirection_path(
            context,
            parsed_command.stdin_path,
        ).read_text(encoding="utf-8")

    stdout_text = ""
    stderr_parts: list[str] = []
    exit_code = 0
    environment = build_subprocess_environment(context)
    for words in parsed_command.pipeline:
        completed = subprocess.run(
            words,
            cwd=context.repo_root,
            input=stage_input,
            text=True,
            capture_output=True,
            env=environment,
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
        stage_input = stdout_text
        if completed.returncode != 0:
            break

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
            if is_git_metadata_path(relative):
                raise ValueError(f"Command argument targets git metadata: {word}")
