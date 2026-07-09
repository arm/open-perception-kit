#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from collections.abc import Iterator
from contextlib import contextmanager
from dataclasses import dataclass
import os
from pathlib import Path
import shutil
import shlex
import subprocess
import tempfile

from ..review.context import ReviewRunContext
from ..runtime_context import AgentRunContext
from .paths import (
    REVIEW_PACKET_DIR,
    is_git_metadata_path,
    is_hidden_review_path,
    reject_hidden_review_path,
    resolve_stdin_redirection_path,
)

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


def configure_review_workspace_environment(environment: dict[str, str], workspace_root: Path) -> None:
    isolated_home = workspace_root.parent / "home"
    environment["HOME"] = str(isolated_home)
    environment["PWD"] = str(workspace_root)
    environment["XDG_CACHE_HOME"] = str(isolated_home / "cache")
    environment["XDG_CONFIG_HOME"] = str(isolated_home / "config")
    environment["XDG_DATA_HOME"] = str(isolated_home / "data")
    environment["GH_CONFIG_DIR"] = str(isolated_home / "gh")


def is_safe_review_source_path(relative: str) -> bool:
    return not is_git_metadata_path(relative) and not is_hidden_review_path(relative)


def iter_review_source_paths(context: ReviewRunContext) -> Iterator[str]:
    completed = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--modified"],
        cwd=context.repo_root,
        text=False,
        capture_output=True,
        timeout=context.command_timeout,
        check=False,
    )
    if completed.returncode == 0:
        seen: set[str] = set()
        for raw_path in completed.stdout.split(b"\0"):
            if not raw_path:
                continue
            relative = raw_path.decode("utf-8", errors="surrogateescape")
            if relative not in seen:
                seen.add(relative)
                yield relative
        return

    for source in context.repo_root.rglob("*"):
        if not source.is_file():
            continue
        relative = source.relative_to(context.repo_root).as_posix()
        if is_safe_review_source_path(relative):
            yield relative


def copy_review_source_file(context: ReviewRunContext, workspace_root: Path, relative: str) -> None:
    relative_path = Path(relative)
    if relative_path.is_absolute() or ".." in relative_path.parts or is_git_metadata_path(relative):
        return
    source = context.repo_root / relative_path
    if source.is_symlink():
        return
    if not source.is_file():
        return
    target = workspace_root / relative_path
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)


def prepare_review_shell_workspace(context: ReviewRunContext, workspace_root: Path) -> None:
    workspace_root.mkdir(parents=True, exist_ok=True)
    for relative in iter_review_source_paths(context):
        copy_review_source_file(context, workspace_root, relative)

    packet_source = context.repo_root / REVIEW_PACKET_DIR
    if packet_source.is_dir():
        packet_target = workspace_root / REVIEW_PACKET_DIR
        packet_target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(packet_source, packet_target, dirs_exist_ok=True)


def build_review_git_environment(
    context: ReviewRunContext,
    workspace_root: Path,
    environment: dict[str, str],
) -> dict[str, str] | None:
    completed = subprocess.run(
        ["git", "rev-parse", "--git-dir"],
        cwd=context.repo_root,
        text=True,
        capture_output=True,
        timeout=context.command_timeout,
        check=False,
    )
    if completed.returncode != 0:
        return None

    git_dir = Path(completed.stdout.strip())
    if not git_dir.is_absolute():
        git_dir = (context.repo_root / git_dir).resolve()

    git_environment = dict(environment)
    git_environment["GIT_DIR"] = str(git_dir)
    git_environment["GIT_WORK_TREE"] = str(workspace_root)

    index_source = git_dir / "index"
    if index_source.is_file():
        index_target = workspace_root.parent / "index"
        shutil.copy2(index_source, index_target)
        git_environment["GIT_INDEX_FILE"] = str(index_target)
    install_review_git_wrapper(
        workspace_root,
        environment,
        git_environment,
        shutil.which("git", path=environment.get("PATH")) or "git",
    )
    return git_environment


def install_review_git_wrapper(
    workspace_root: Path,
    environment: dict[str, str],
    git_environment: dict[str, str],
    git_executable: str,
) -> None:
    bin_dir = workspace_root.parent / "bin"
    bin_dir.mkdir(parents=True, exist_ok=True)
    wrapper = bin_dir / "git"
    wrapper.write_text(
        "\n".join(
            [
                "#!/usr/bin/env python3",
                "import os",
                "from pathlib import Path",
                "import sys",
                "",
                f"GIT_EXECUTABLE = {git_executable!r}",
                f"WORKSPACE_ROOT = {str(workspace_root)!r}",
                f"GIT_ENVIRONMENT = {git_environment!r}",
                "READ_ONLY_SUBCOMMANDS = {",
                *[f"    {subcommand!r}," for subcommand in sorted(READ_ONLY_GIT_SUBCOMMANDS | {
                    "show-ref",
                    "symbolic-ref",
                })],
                "}",
                f"FORBIDDEN_OPTIONS = {sorted(FORBIDDEN_GIT_OPTIONS - {'-C'})!r}",
                "",
                "",
                "def fail(message):",
                "    print(message, file=sys.stderr)",
                "    sys.exit(128)",
                "",
                "",
                "def option_has_inline_value(word, option):",
                "    if option.startswith('--'):",
                "        return word.startswith(f'{option}=')",
                "    return word.startswith(option) and word != option",
                "",
                "",
                "def reject_forbidden_options(args):",
                "    for word in args:",
                "        for option in FORBIDDEN_OPTIONS:",
                "            if word == option or option_has_inline_value(word, option):",
                "                fail(f'Blocked git option in review shell: {option}')",
                "",
                "",
                "def effective_cwd(args):",
                "    resolved = Path.cwd().resolve()",
                "    index = 0",
                "    while index < len(args):",
                "        if args[index] != '-C':",
                "            index += 1",
                "            continue",
                "        if index + 1 >= len(args):",
                "            fail('Missing path after git -C')",
                "        value = Path(args[index + 1])",
                "        resolved = (value if value.is_absolute() else resolved / value).resolve()",
                "        index += 2",
                "    return resolved",
                "",
                "",
                "def is_in_workspace(path):",
                "    workspace = Path(WORKSPACE_ROOT).resolve()",
                "    return path == workspace or workspace in path.parents",
                "",
                "",
                "def passthrough(args):",
                "    environment = dict(os.environ)",
                "    for name in GIT_ENVIRONMENT:",
                "        environment.pop(name, None)",
                "    os.execvpe(GIT_EXECUTABLE, [GIT_EXECUTABLE, *args], environment)",
                "",
                "",
                "def find_subcommand(args):",
                "    index = 0",
                "    while index < len(args):",
                "        word = args[index]",
                "        if word == '--':",
                "            index += 1",
                "            continue",
                "        if word == '-C':",
                "            index += 2",
                "            continue",
                "        if word.startswith('-'):",
                "            index += 1",
                "            continue",
                "        return word",
                "    return None",
                "",
                "",
                "def symbolic_ref_is_read_only(args):",
                "    try:",
                "        index = args.index('symbolic-ref') + 1",
                "    except ValueError:",
                "        return True",
                "    if any(word in {'--delete', '-d', '-m'} for word in args[index:]):",
                "        return False",
                "    refs = [word for word in args[index:] if not word.startswith('-')]",
                "    return len(refs) <= 1",
                "",
                "",
                "def apply_is_read_only(args):",
                "    try:",
                "        index = args.index('apply') + 1",
                "    except ValueError:",
                "        return True",
                "    return '--check' in args[index:]",
                "",
                "",
                "args = sys.argv[1:]",
                "if not is_in_workspace(effective_cwd(args)):",
                "    passthrough(args)",
                "reject_forbidden_options(args)",
                "subcommand = find_subcommand(args)",
                "if subcommand == 'apply' and not apply_is_read_only(args):",
                "    fail('Blocked mutating git apply in review shell')",
                "if subcommand != 'apply' and subcommand not in READ_ONLY_SUBCOMMANDS:",
                "    fail(f'Blocked git subcommand in review shell: {subcommand}')",
                "if subcommand == 'symbolic-ref' and not symbolic_ref_is_read_only(args):",
                "    fail('Blocked mutating git symbolic-ref in review shell')",
                "environment = dict(os.environ)",
                "environment.update(GIT_ENVIRONMENT)",
                "os.execvpe(GIT_EXECUTABLE, [GIT_EXECUTABLE, *args], environment)",
                "",
            ]
        ),
        encoding="utf-8",
    )
    wrapper.chmod(0o755)
    environment["PATH"] = f"{bin_dir}{os.pathsep}{environment.get('PATH', '')}"
    git_environment["PATH"] = environment["PATH"]


@contextmanager
def shell_command_scope(context: AgentRunContext) -> Iterator[tuple[Path, dict[str, str], dict[str, str] | None]]:
    environment = build_subprocess_environment(context)
    if not isinstance(context, ReviewRunContext):
        yield context.repo_root, environment, None
        return

    with tempfile.TemporaryDirectory(prefix="agent-review-shell-") as temp_dir:
        workspace_root = Path(temp_dir) / "repo"
        prepare_review_shell_workspace(context, workspace_root)
        configure_review_workspace_environment(environment, workspace_root)
        git_environment = build_review_git_environment(context, workspace_root, environment)
        yield workspace_root, environment, git_environment


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


def is_git_command(words: list[str]) -> bool:
    return bool(words) and Path(words[0]).name == "git"


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


def run_parsed_shell_command(
    parsed_command: ParsedShellCommand,
    context: AgentRunContext,
    cwd: Path,
    environment: dict[str, str],
    git_environment: dict[str, str] | None,
) -> ShellCommandResult:
    stage_input: str | None = None
    if parsed_command.stdin_path:
        if isinstance(context, ReviewRunContext):
            stdin_path = resolve_shell_workspace_path(
                parsed_command.stdin_path,
                cwd,
                "Shell stdin redirection",
            )
            if not stdin_path.is_file():
                raise ValueError(
                    f"Shell stdin redirection path is not available in the shell workspace: "
                    f"{parsed_command.stdin_path}"
                )
        else:
            stdin_path = resolve_stdin_redirection_path(
                context,
                parsed_command.stdin_path,
            )
        stage_input = stdin_path.read_text(encoding="utf-8")

    stdout_text = ""
    stderr_parts: list[str] = []
    exit_code = 0
    for words in parsed_command.pipeline:
        completed = subprocess.run(
            words,
            cwd=cwd,
            input=stage_input,
            text=True,
            capture_output=True,
            env=git_environment if git_environment is not None and is_git_command(words) else environment,
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


def is_path_like_shell_word(word: str) -> bool:
    return word in {".", ".."} or "/" in word


def resolve_shell_workspace_path(path_value: str, cwd: Path, operation: str) -> Path:
    path = Path(path_value)
    resolved = (path if path.is_absolute() else cwd / path).resolve()
    if resolved != cwd and cwd not in resolved.parents:
        raise ValueError(f"{operation} path escapes shell workspace: {path_value}")
    return resolved


def reject_shell_path_arguments(
    parsed_command: ParsedShellCommand,
    context: AgentRunContext,
    cwd: Path,
) -> None:
    for words in parsed_command.pipeline:
        for index, word in enumerate(words):
            if not word or word.startswith("-") or "://" in word:
                continue
            if isinstance(context, ReviewRunContext) and index > 0 and is_path_like_shell_word(word):
                resolve_shell_workspace_path(word, cwd, "Command argument")
            try:
                path = context.resolve_repo_path(word)
                relative = path.relative_to(context.repo_root).as_posix()
            except ValueError:
                continue
            if is_git_metadata_path(relative):
                raise ValueError(f"Command argument targets git metadata: {word}")
            if index > 0 and isinstance(context, ReviewRunContext):
                reject_hidden_review_path(relative, "Command argument")
