#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from .process import CommandRunner, run_command


def remote_branch_ref(branch: str) -> str:
    return f"refs/heads/{branch}"


def remote_branch_force_lease(branch: str, *, command_runner: CommandRunner = run_command) -> str:
    branch_ref = remote_branch_ref(branch)
    result = command_runner(
        ["git", "ls-remote", "--heads", "origin", branch_ref],
        capture_output=True,
    )
    lines = [line.split() for line in result.stdout.splitlines() if line.strip()]
    if not lines:
        return f"{branch_ref}:"
    if len(lines) != 1 or len(lines[0]) != 2 or lines[0][1] != branch_ref:
        raise RuntimeError(f"Could not resolve remote repair branch lease for {branch}.")
    return f"{branch_ref}:{lines[0][0]}"


def push_head_to_remote_branch(
    branch: str,
    *,
    branch_lease: str,
    command_runner: CommandRunner = run_command,
) -> None:
    command_runner(
        [
            "git",
            "push",
            f"--force-with-lease={branch_lease}",
            "--set-upstream",
            "origin",
            f"HEAD:{remote_branch_ref(branch)}",
        ]
    )
