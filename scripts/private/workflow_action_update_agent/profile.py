#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from pathlib import Path

from agent_runtime.contracts import (
    AgentCommand,
    AgentInstance,
    load_json_object,
    require_list,
    require_non_empty_string,
)
from agent_runtime.config.model import resolve_agent_model
from agent_runtime.config.task import AgentTaskSettings, resolve_agent_task_settings

from .paths import REPO_ROOT, default_profile_path_argument, profile_config_root, resolve_repo_path
from .validation import CANONICAL_VALIDATION_COMMAND_SETS


def load_profile(profile_path: str = "") -> dict[str, object]:
    path = resolve_repo_path(profile_path or default_profile_path_argument())
    if not path.is_file():
        raise ValueError(f"Workflow action update agent profile is missing: {path}")
    profile = load_json_object(path, "Workflow action update agent profile")

    required_string_keys = (
        "display_name",
        "automation_name",
        "repair_branch_template",
        "repair_branch_guard_regex",
        "repair_authorization_label",
        "pr_trigger_label",
        "pr_title_template",
        "commit_subject_template",
        "commit_notes_template",
        "pr_description_template",
        "agent_model_config",
        "agent_task_config",
        "validation_command_set",
    )
    required_list_keys = (
        "prompt_context_files",
        "repair_definition_of_done",
    )
    optional_bool_keys = ("require_failure_conclusion",)
    allowed_keys = set(required_string_keys) | set(required_list_keys) | set(optional_bool_keys)
    unsupported_keys = sorted(set(profile) - allowed_keys)
    if unsupported_keys:
        raise ValueError(
            "Workflow action update agent profile contains unsupported keys: "
            + ", ".join(unsupported_keys)
        )

    for key in required_string_keys:
        profile_string(profile, key)
    for key in required_list_keys:
        profile_list(profile, key)
    for key in optional_bool_keys:
        if key in profile:
            profile_bool(profile, key, True)

    profile_validation_commands(profile)
    return profile


def profile_bool(profile: dict[str, object], key: str, default: bool) -> bool:
    value = profile.get(key, default)
    if not isinstance(value, bool):
        raise ValueError(f"Profile key '{key}' must be a boolean.")
    return value


def profile_string(profile: dict[str, object], key: str) -> str:
    try:
        return require_non_empty_string(profile.get(key), key)
    except ValueError as exc:
        raise ValueError(f"Profile key '{key}' must be a non-empty string.") from exc


def profile_runtime_config_path(profile: dict[str, object], key: str, profile_path: str = "") -> Path:
    config_path = Path(profile_string(profile, key))
    if not config_path.is_absolute():
        config_path = profile_config_root(profile_path) / config_path
    return config_path.resolve()


def profile_runtime_config_file(profile: dict[str, object], key: str, profile_path: str = "") -> str:
    config_path = profile_runtime_config_path(profile, key, profile_path)
    try:
        return config_path.relative_to(REPO_ROOT).as_posix()
    except ValueError:
        return config_path.as_posix()


def profile_agent_model_config_path(profile: dict[str, object], profile_path: str = "") -> Path:
    return profile_runtime_config_path(profile, "agent_model_config", profile_path)


def profile_agent_model_config_file(profile: dict[str, object], profile_path: str = "") -> str:
    return profile_runtime_config_file(profile, "agent_model_config", profile_path)


def profile_agent_model(profile: dict[str, object], agent_instance: AgentInstance, profile_path: str = "") -> str:
    return resolve_agent_model(
        profile_agent_model_config_path(profile, profile_path),
        agent_instance,
    )


def profile_agent_task_config_path(profile: dict[str, object], profile_path: str = "") -> Path:
    return profile_runtime_config_path(profile, "agent_task_config", profile_path)


def profile_agent_task_config_file(profile: dict[str, object], profile_path: str = "") -> str:
    return profile_runtime_config_file(profile, "agent_task_config", profile_path)


def profile_agent_task_settings(
    profile: dict[str, object],
    command: AgentCommand,
    profile_path: str = "",
) -> AgentTaskSettings:
    return resolve_agent_task_settings(
        profile_agent_task_config_path(profile, profile_path),
        command,
    )


def profile_agent_runtime_config_outputs(
    profile: dict[str, object],
    *,
    command: AgentCommand,
    profile_path: str = "",
) -> dict[str, str]:
    task_settings = profile_agent_task_settings(profile, command, profile_path)
    profile_agent_model(profile, task_settings.agent_instance, profile_path)
    return {
        "agent_model_config_file": profile_agent_model_config_file(profile, profile_path),
        "agent_task_config_file": profile_agent_task_config_file(profile, profile_path),
    }


def profile_list(profile: dict[str, object], key: str) -> list[object]:
    try:
        return require_list(profile.get(key), key)
    except ValueError as exc:
        raise ValueError(f"Profile key '{key}' must be a JSON array.") from exc


def profile_string_list(profile: dict[str, object], key: str) -> list[str]:
    items = profile_list(profile, key)
    if not all(isinstance(item, str) and item.strip() for item in items):
        raise ValueError(f"Profile key '{key}' must contain only non-empty strings.")
    return [str(item) for item in items]


def profile_validation_commands(profile: dict[str, object]) -> list[str]:
    command_set = profile_string(profile, "validation_command_set")
    commands = CANONICAL_VALIDATION_COMMAND_SETS.get(command_set)
    if commands is None:
        allowed = ", ".join(sorted(CANONICAL_VALIDATION_COMMAND_SETS))
        raise ValueError(f"Unknown validation_command_set '{command_set}'. Expected one of: {allowed}.")
    return list(commands)
