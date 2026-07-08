#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import os
import shlex

from .process import run_command


AGENT_WORKFLOW_VALIDATION_COMMAND_SET = "agent-workflow-python"
CANONICAL_VALIDATION_COMMAND_SETS = {
    AGENT_WORKFLOW_VALIDATION_COMMAND_SET: (
        "python3 -m unittest discover -s scripts/private/tests",
        "python3 -m unittest discover -s scripts/private/agent_runtime/tests",
        "python3 -m unittest discover -s scripts/private/agent_repair_orchestrator/tests",
        "python3 -m unittest discover -s scripts/private/agent_stabilization_orchestrator/tests",
        "python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_agent_static_analysis.py'",
        "python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_detect_secrets_quality_flow.py'",
        "python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_agent_workflow_contracts.py'",
        "git diff --stat",
    ),
}
VALIDATION_ENV_BLOCKLIST = (
    "GITHUB_ENV",
    "GITHUB_OUTPUT",
    "GITHUB_PATH",
    "GITHUB_STEP_SUMMARY",
)
VALIDATION_ENV_SENSITIVE_FRAGMENTS = (
    "AUTH",
    "CREDENTIAL",
    "KEY",
    "PASS",
    "PRIVATE_KEY",
    "SECRET",
    "TOKEN",
)
VALIDATION_COMMAND_ALLOWLIST = {
    tuple(shlex.split(command))
    for commands in CANONICAL_VALIDATION_COMMAND_SETS.values()
    for command in commands
}


def validation_command_args(command: str) -> list[str]:
    try:
        args = shlex.split(command)
    except ValueError as exc:
        raise ValueError(f"Validation command is not valid argv text: {command}") from exc
    if tuple(args) not in VALIDATION_COMMAND_ALLOWLIST:
        raise ValueError(f"Validation command is not in the trusted allowlist: {command}")
    return args


def run_validation_command(command: str, *, env: dict[str, str] | None = None) -> None:
    run_command(validation_command_args(command), env=env)


def run_validation_commands(commands: list[str]) -> None:
    env = validation_command_environment()
    for command in commands:
        print(f"Running validation command: {command}")
        run_validation_command(command, env=env)


def validation_command_environment() -> dict[str, str]:
    blocked_names = set(VALIDATION_ENV_BLOCKLIST)
    return {
        key: value
        for key, value in os.environ.items()
        if key.upper() not in blocked_names
        and not any(fragment in key.upper() for fragment in VALIDATION_ENV_SENSITIVE_FRAGMENTS)
    }
