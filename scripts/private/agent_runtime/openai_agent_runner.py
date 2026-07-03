#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import asyncio
import os
import sys
from pathlib import Path

if __package__ in (None, ""):  # pragma: no cover - used for direct script execution.
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    __package__ = "agent_runtime"

from .tasks.configured import (
    ConfiguredAgentWorkflowTask,
    RepositoryEditAgentTask,
    ReviewAgentTask,
)
from .contracts import (
    AgentCommand,
    DEFAULT_AGENT_MODEL_CONFIG_PATH,
    DEFAULT_AGENT_TASK_CONFIG_PATH,
    AgentInstance,
)
from .config.model import resolve_agent_model
from .runtime_context import set_run_context
from .sdk_runtime import configure_openai_environment
from .config.task import (
    AgentTaskSettings,
    load_agent_task_config,
    resolve_agent_task_settings,
)

AGENT_TASKS: tuple[ConfiguredAgentWorkflowTask, ...] = (
    ReviewAgentTask(),
    RepositoryEditAgentTask(
        command=AgentCommand.REPAIR,
        agent_name="OpenAI SDK Workflow Repair Agent",
    ),
    RepositoryEditAgentTask(
        command=AgentCommand.STABILIZATION,
        agent_name="OpenAI SDK Workflow Stabilization Agent",
    ),
)


def iter_agent_tasks() -> tuple[ConfiguredAgentWorkflowTask, ...]:
    return AGENT_TASKS


def get_agent_task(command: str | AgentCommand) -> ConfiguredAgentWorkflowTask:
    parsed_command = AgentCommand(command)
    for task in AGENT_TASKS:
        if task.command is parsed_command:
            return task
    raise ValueError(f"Unsupported agent command: {parsed_command.value}")


def validate_agent_task_registry(task_config_file: str | Path) -> None:
    commands = [task.command for task in AGENT_TASKS]
    duplicate_commands = sorted(
        {
            command.value
            for index, command in enumerate(commands)
            if command in commands[:index]
        }
    )
    if duplicate_commands:
        raise ValueError(
            "Agent task registry has duplicate commands: "
            + ", ".join(duplicate_commands)
        )

    configured_commands = set(load_agent_task_config(task_config_file).tasks)
    registered_commands = set(commands)
    missing_tasks = sorted(
        command.value
        for command in configured_commands - registered_commands
    )
    unconfigured_tasks = sorted(
        command.value
        for command in registered_commands - configured_commands
    )
    if missing_tasks or unconfigured_tasks:
        details = []
        if missing_tasks:
            details.append("missing Python task(s): " + ", ".join(missing_tasks))
        if unconfigured_tasks:
            details.append("missing config task(s): " + ", ".join(unconfigured_tasks))
        raise ValueError(
            "Agent task registry and config do not match: "
            + "; ".join(details)
        )


def resolve_runner_model(args: argparse.Namespace) -> str:
    return resolve_agent_model(
        args.model_config_file,
        AgentInstance(args.agent_instance) if args.agent_instance else args.task_settings.agent_instance,
        override_model=args.model,
    )


def add_common_task_arguments(subparser: argparse.ArgumentParser) -> None:
    subparser.add_argument("--prompt-file", required=True)
    subparser.add_argument("--output-file", required=True)
    subparser.add_argument("--model", default="")
    subparser.add_argument("--model-config-file", default=DEFAULT_AGENT_MODEL_CONFIG_PATH)
    subparser.add_argument("--task-config-file", default=DEFAULT_AGENT_TASK_CONFIG_PATH)
    subparser.add_argument(
        "--agent-instance",
        choices=[instance.value for instance in AgentInstance],
        default=None,
    )
    subparser.add_argument("--repo-root", default=os.getcwd())
    subparser.add_argument(
        "--max-turns",
        type=int,
        default=None,
        help="Maximum main-agent turns. Defaults to the command-specific task config limit.",
    )
    subparser.add_argument(
        "--task-estimate-turns",
        type=int,
        default=None,
        help="Maximum turns for the generic preflight task estimator.",
    )
    subparser.add_argument(
        "--max-prompt-chars",
        type=int,
        default=None,
        help="Maximum prompt size before the estimator blocks the main agent run.",
    )
    subparser.add_argument(
        "--max-review-files",
        type=int,
        default=None,
        help="Advisory changed file count for review tasks.",
    )
    subparser.add_argument(
        "--max-review-changed-lines",
        type=int,
        default=None,
        help="Advisory changed line count for review tasks.",
    )
    subparser.add_argument("--command-timeout", type=int, default=300)


def resolve_task_settings(args: argparse.Namespace) -> AgentTaskSettings:
    return resolve_agent_task_settings(
        args.task_config_file,
        AgentCommand(args.command),
        agent_instance_override=args.agent_instance,
        max_turns_override=args.max_turns,
        task_estimate_turns_override=args.task_estimate_turns,
        max_prompt_chars_override=args.max_prompt_chars,
        max_review_files_override=args.max_review_files,
        max_review_changed_lines_override=args.max_review_changed_lines,
    )


async def run_command(args: argparse.Namespace) -> int:
    configure_openai_environment()
    set_run_context(Path(args.repo_root), args.command_timeout)
    validate_agent_task_registry(args.task_config_file)
    args.task_settings = resolve_task_settings(args)
    args.agent_instance = args.agent_instance or args.task_settings.agent_instance.value
    args.resolved_model = resolve_runner_model(args)
    task = get_agent_task(args.command)
    return await task.run(args)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run amp-dev-forge OpenAI SDK agent workflows.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    for task in iter_agent_tasks():
        subparser = subparsers.add_parser(task.command.value)
        add_common_task_arguments(subparser)
        task.add_cli_arguments(subparser)

    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        return asyncio.run(run_command(args))
    except Exception as exc:  # noqa: BLE001
        print(f"OpenAI SDK agent run failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
