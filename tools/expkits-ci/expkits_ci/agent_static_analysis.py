#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

if __package__ in (None, ""):  # pragma: no cover - used for direct script execution.
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    __package__ = "expkits_ci"


AGENT_STATIC_PYTHON_PATHS = (
    "scripts/private/github_actions.py",
    "scripts/private/github_api.py",
    "scripts/private/github_pr_context.py",
    "scripts/private/sonar_quality_gate_workflow.py",
    "scripts/private/agent_runtime",
    "scripts/private/agent_repair_orchestrator",
    "scripts/private/agent_stabilization_orchestrator",
    "scripts/private/agent_workflow_common",
    "scripts/private/test_support",
    "scripts/private/tests",
    "tools/expkits-ci/expkits_ci/agent_static_analysis.py",
    "tools/expkits-ci/tests/test_agent_static_analysis.py",
    "tools/expkits-ci/tests/test_agent_workflow_contracts.py",
)
# Ignore files intentionally name absent/generated paths, not source references.
AGENT_STATIC_REFERENCE_PATHS = (
    ".github",
    "scripts/private",
    "tools/expkits-ci/agent-workflows-mypy.ini",
    "tools/expkits-ci/tests/test_agent_static_analysis.py",
    "tools/expkits-ci/tests/test_agent_workflow_contracts.py",
)


def repo_path(repo_root: Path, path_value: str) -> Path:
    return (repo_root / path_value).resolve()


def existing_paths(repo_root: Path, path_values: tuple[str, ...]) -> list[str]:
    paths = []
    for path_value in path_values:
        path = repo_path(repo_root, path_value)
        if path.exists():
            paths.append(path_value)
    return paths


def run_command(repo_root: Path, command: list[str], label: str) -> bool:
    print(f"Running {label}: {' '.join(command)}")
    completed = subprocess.run(
        command,
        cwd=repo_root,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if completed.stdout:
        print(completed.stdout.rstrip())
    if completed.returncode != 0:
        print(f"{label} failed with exit code {completed.returncode}.", file=sys.stderr)
        return False
    return True


def run_mypy(repo_root: Path) -> bool:
    return run_command(
        repo_root,
        [
            sys.executable,
            "-m",
            "mypy",
            "--config-file",
            "tools/expkits-ci/agent-workflows-mypy.ini",
        ],
        "mypy",
    )


def run_pyflakes(repo_root: Path) -> bool:
    paths = existing_paths(repo_root, AGENT_STATIC_PYTHON_PATHS)
    if not paths:
        print("No Agent runtime Python files found for pyflakes.")
        return True
    return run_command(repo_root, [sys.executable, "-m", "pyflakes", *paths], "pyflakes")


def run_vulture(repo_root: Path) -> bool:
    paths = existing_paths(repo_root, AGENT_STATIC_PYTHON_PATHS)
    if not paths:
        print("No Agent runtime Python files found for vulture.")
        return True
    return run_command(
        repo_root,
        [sys.executable, "-m", "vulture", *paths, "--min-confidence", "100"],
        "vulture",
    )


def git_output(repo_root: Path, command: list[str]) -> str:
    completed = subprocess.run(
        command,
        cwd=repo_root,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=False,
    )
    if completed.returncode != 0:
        stderr = completed.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(f"Command failed: {' '.join(command)}\n{stderr}")
    return completed.stdout.decode("utf-8", errors="replace")


def parse_removed_or_renamed_paths(name_status_output: str) -> list[str]:
    tokens = [token for token in name_status_output.split("\0") if token]
    removed_paths: list[str] = []
    index = 0
    while index < len(tokens):
        status = tokens[index]
        index += 1
        if status.startswith("R"):
            if index + 1 >= len(tokens):
                break
            old_path = tokens[index]
            index += 2
            removed_paths.append(old_path)
            continue
        if index >= len(tokens):
            break
        path = tokens[index]
        index += 1
        if status.startswith("D"):
            removed_paths.append(path)
    return removed_paths


def removed_or_renamed_paths(repo_root: Path, *, base_ref: str, staged: bool) -> list[str]:
    if base_ref:
        command = ["git", "diff", "--name-status", "-z", f"{base_ref}...HEAD"]
    elif staged:
        command = ["git", "diff", "--cached", "--name-status", "-z"]
    else:
        return []
    return parse_removed_or_renamed_paths(git_output(repo_root, command))


def reference_tokens_for_removed_path(path_value: str) -> set[str]:
    path = Path(path_value)
    tokens = {path_value}
    if path.suffix:
        tokens.add(path_value[: -len(path.suffix)])
    return {token for token in tokens if token}


def tracked_reference_files(repo_root: Path) -> list[str]:
    output = git_output(repo_root, ["git", "ls-files", "-z", *AGENT_STATIC_REFERENCE_PATHS])
    return [path for path in output.split("\0") if path and repo_path(repo_root, path).is_file()]


def find_removed_reference_violations(
    repo_root: Path,
    removed_paths: list[str],
) -> list[str]:
    tokens = sorted(
        {
            token
            for path in removed_paths
            for token in reference_tokens_for_removed_path(path)
        },
        key=lambda token: (-len(token), token),
    )
    if not tokens:
        return []

    violations: list[str] = []
    for path in tracked_reference_files(repo_root):
        file_path = repo_path(repo_root, path)
        try:
            lines = file_path.read_text(encoding="utf-8").splitlines()
        except UnicodeDecodeError:
            continue
        for line_number, line in enumerate(lines, start=1):
            for token in tokens:
                if token in line:
                    violations.append(f"{path}:{line_number}: removed path reference '{token}'")
    return violations


def check_removed_reference_leaks(repo_root: Path, *, base_ref: str, staged: bool) -> bool:
    removed_paths = removed_or_renamed_paths(repo_root, base_ref=base_ref, staged=staged)
    if not removed_paths:
        print("No removed or renamed source paths to check for stale references.")
        return True

    violations = find_removed_reference_violations(repo_root, removed_paths)
    if not violations:
        print("No stale references to removed or renamed source paths found.")
        return True

    print("Found stale references to removed or renamed source paths:", file=sys.stderr)
    for violation in violations:
        print(violation, file=sys.stderr)
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description="Run Agent workflow static analysis.")
    parser.add_argument("--repo-root", default=".")
    parser.add_argument("--base-ref", default="")
    parser.add_argument("--staged", action="store_true")
    parser.add_argument("--skip-mypy", action="store_true")
    parser.add_argument("--skip-pyflakes", action="store_true")
    parser.add_argument("--skip-vulture", action="store_true")
    parser.add_argument("--skip-removed-reference-check", action="store_true")
    args = parser.parse_args()

    repo_root = Path(args.repo_root).resolve()
    checks = []
    if not args.skip_mypy:
        checks.append(run_mypy(repo_root))
    if not args.skip_pyflakes:
        checks.append(run_pyflakes(repo_root))
    if not args.skip_vulture:
        checks.append(run_vulture(repo_root))
    if not args.skip_removed_reference_check:
        checks.append(
            check_removed_reference_leaks(
                repo_root,
                base_ref=args.base_ref,
                staged=args.staged,
            )
        )
    return 0 if all(checks) else 1


if __name__ == "__main__":
    raise SystemExit(main())
