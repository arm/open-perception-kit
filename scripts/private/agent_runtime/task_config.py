#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path

from .contracts import (
    AgentCommand,
    AgentInstance,
    DEFAULT_AGENT_TASK_CONFIG_PATH,
    load_json_object,
    optional_positive_int,
    parse_enum_value,
    require_non_empty_string,
    require_object,
    require_positive_int,
)


@dataclass(frozen=True)
class AgentTaskConfigEntry:
    agent_instance: AgentInstance
    max_turns: int
    task_estimate_turns: int
    max_prompt_chars: int
    max_review_files: int | None = None
    max_review_changed_lines: int | None = None


@dataclass(frozen=True)
class AgentTaskConfig:
    tasks: dict[AgentCommand, AgentTaskConfigEntry]


@dataclass(frozen=True)
class AgentTaskSettings:
    command: AgentCommand
    agent_instance: AgentInstance
    max_turns: int
    task_estimate_turns: int
    max_prompt_chars: int
    max_review_files: int | None = None
    max_review_changed_lines: int | None = None


def load_agent_task_config(config_file: str | Path) -> AgentTaskConfig:
    payload = load_json_object(config_file, "Agent task config")
    tasks_payload = require_object(payload.get("tasks"), "tasks")

    tasks: dict[AgentCommand, AgentTaskConfigEntry] = {}
    for raw_command, raw_entry in tasks_payload.items():
        command = parse_enum_value(AgentCommand, raw_command, "agent command")
        entry = require_object(raw_entry, f"tasks.{raw_command}")
        tasks[command] = AgentTaskConfigEntry(
            agent_instance=parse_enum_value(
                AgentInstance,
                require_non_empty_string(entry.get("agent_instance"), f"tasks.{raw_command}.agent_instance"),
                "agent instance",
            ),
            max_turns=require_positive_int(entry.get("max_turns"), f"tasks.{raw_command}.max_turns"),
            task_estimate_turns=require_positive_int(
                entry.get("task_estimate_turns"),
                f"tasks.{raw_command}.task_estimate_turns",
            ),
            max_prompt_chars=require_positive_int(
                entry.get("max_prompt_chars"),
                f"tasks.{raw_command}.max_prompt_chars",
            ),
            max_review_files=optional_positive_int(
                entry.get("max_review_files"),
                f"tasks.{raw_command}.max_review_files",
            ),
            max_review_changed_lines=optional_positive_int(
                entry.get("max_review_changed_lines"),
                f"tasks.{raw_command}.max_review_changed_lines",
            ),
        )

    return AgentTaskConfig(tasks=tasks)


def positive_limit(value: int | None, default: int, name: str) -> int:
    resolved = default if value is None else value
    if resolved <= 0:
        raise ValueError(f"{name} must be a positive integer.")
    return resolved


def optional_positive_limit(value: int | None, default: int | None, name: str) -> int | None:
    resolved = default if value is None else value
    if resolved is not None and resolved <= 0:
        raise ValueError(f"{name} must be a positive integer.")
    return resolved


def resolve_agent_task_settings(
    config_file: str | Path,
    command: str | AgentCommand,
    *,
    agent_instance_override: str | None = None,
    max_turns_override: int | None = None,
    task_estimate_turns_override: int | None = None,
    max_prompt_chars_override: int | None = None,
    max_review_files_override: int | None = None,
    max_review_changed_lines_override: int | None = None,
) -> AgentTaskSettings:
    parsed_command = parse_enum_value(AgentCommand, command, "agent command")
    config = load_agent_task_config(config_file)
    entry = config.tasks.get(parsed_command)
    if entry is None:
        raise ValueError(f"Agent task config does not define command: {parsed_command.value}")

    if agent_instance_override:
        agent_instance = parse_enum_value(AgentInstance, agent_instance_override, "agent instance")
    else:
        agent_instance = entry.agent_instance

    return AgentTaskSettings(
        command=parsed_command,
        agent_instance=agent_instance,
        max_turns=positive_limit(max_turns_override, entry.max_turns, "--max-turns"),
        task_estimate_turns=positive_limit(
            task_estimate_turns_override,
            entry.task_estimate_turns,
            "--task-estimate-turns",
        ),
        max_prompt_chars=positive_limit(
            max_prompt_chars_override,
            entry.max_prompt_chars,
            "--max-prompt-chars",
        ),
        max_review_files=optional_positive_limit(
            max_review_files_override,
            entry.max_review_files,
            "--max-review-files",
        ),
        max_review_changed_lines=optional_positive_limit(
            max_review_changed_lines_override,
            entry.max_review_changed_lines,
            "--max-review-changed-lines",
        ),
    )


def resolve_agent_max_turns(command: AgentCommand, args: argparse.Namespace) -> int:
    settings = resolve_agent_task_settings(
        getattr(args, "task_config_file", DEFAULT_AGENT_TASK_CONFIG_PATH),
        command,
        agent_instance_override=getattr(args, "agent_instance", None),
        max_turns_override=getattr(args, "max_turns", None),
        task_estimate_turns_override=getattr(args, "task_estimate_turns", None),
        max_prompt_chars_override=getattr(args, "max_prompt_chars", None),
        max_review_files_override=getattr(args, "max_review_files", None),
        max_review_changed_lines_override=getattr(args, "max_review_changed_lines", None),
    )
    return settings.max_turns
