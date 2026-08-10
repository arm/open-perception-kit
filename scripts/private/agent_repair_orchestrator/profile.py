#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from pathlib import Path

from agent_runtime.contracts import AgentCommand, AgentInstance
from agent_runtime.config.task import AgentTaskSettings
from agent_workflow_common import profile as common_profile

from .paths import REPO_ROOT, default_profile_path_argument, profile_config_root, resolve_repo_path


PROFILE_DESCRIPTION = "Agent source-run repair profile"
PROFILE_PATH_RESOLVER = common_profile.ProfilePathResolver(
    repo_root=REPO_ROOT,
    profile_config_root=profile_config_root,
)


def load_profile(profile_path: str = "") -> dict[str, object]:
    profile = common_profile.load_profile_json(
        profile_path,
        default_profile_path_argument=default_profile_path_argument,
        resolve_repo_path=resolve_repo_path,
        description=PROFILE_DESCRIPTION,
    )
    common_profile.validate_profile_schema(
        profile,
        description=PROFILE_DESCRIPTION,
        required_string_keys=(
            "display_name",
            "automation_name",
            "repair_branch_template",
            "repair_branch_guard_regex",
            "repair_authorization_label",
            "pr_title_template",
            "commit_subject_template",
            "commit_notes_template",
            "pr_description_template",
            "agent_model_config",
            "agent_task_config",
            "validation_command_set",
        ),
        required_list_keys=(
            "prompt_context_files",
            "repair_definition_of_done",
            "pr_trigger_labels",
        ),
        optional_bool_keys=("require_failure_conclusion",),
    )
    return profile


def profile_bool(profile: dict[str, object], key: str, default: bool) -> bool:
    return common_profile.profile_bool(profile, key, default)


def profile_string(profile: dict[str, object], key: str) -> str:
    return common_profile.profile_string(profile, key)


def profile_runtime_config_path(profile: dict[str, object], key: str, profile_path: str = "") -> Path:
    return common_profile.profile_runtime_config_path(
        profile,
        key,
        profile_path,
        PROFILE_PATH_RESOLVER,
    )


def profile_runtime_config_file(profile: dict[str, object], key: str, profile_path: str = "") -> str:
    return common_profile.profile_runtime_config_file(
        profile,
        key,
        profile_path,
        PROFILE_PATH_RESOLVER,
    )


def profile_agent_model_config_path(profile: dict[str, object], profile_path: str = "") -> Path:
    return common_profile.profile_agent_model_config_path(
        profile,
        profile_path,
        PROFILE_PATH_RESOLVER,
    )


def profile_agent_model_config_file(profile: dict[str, object], profile_path: str = "") -> str:
    return common_profile.profile_agent_model_config_file(
        profile,
        profile_path,
        PROFILE_PATH_RESOLVER,
    )


def profile_agent_model(profile: dict[str, object], agent_instance: AgentInstance, profile_path: str = "") -> str:
    return common_profile.profile_agent_model(
        profile,
        agent_instance,
        profile_path,
        PROFILE_PATH_RESOLVER,
    )


def profile_agent_task_config_path(profile: dict[str, object], profile_path: str = "") -> Path:
    return common_profile.profile_agent_task_config_path(
        profile,
        profile_path,
        PROFILE_PATH_RESOLVER,
    )


def profile_agent_task_config_file(profile: dict[str, object], profile_path: str = "") -> str:
    return common_profile.profile_agent_task_config_file(
        profile,
        profile_path,
        PROFILE_PATH_RESOLVER,
    )


def profile_agent_task_settings(
    profile: dict[str, object],
    command: AgentCommand,
    profile_path: str = "",
) -> AgentTaskSettings:
    return common_profile.profile_agent_task_settings(
        profile,
        command,
        profile_path,
        PROFILE_PATH_RESOLVER,
    )


def profile_agent_runtime_config_outputs(
    profile: dict[str, object],
    *,
    command: AgentCommand,
    profile_path: str = "",
) -> dict[str, str]:
    return common_profile.profile_agent_runtime_config_outputs(
        profile,
        command=command,
        profile_path=profile_path,
        resolver=PROFILE_PATH_RESOLVER,
    )


def profile_list(profile: dict[str, object], key: str) -> list[object]:
    return common_profile.profile_list(profile, key)


def profile_string_list(profile: dict[str, object], key: str) -> list[str]:
    return common_profile.profile_string_list(profile, key)


def profile_validation_commands(profile: dict[str, object]) -> list[str]:
    return common_profile.profile_validation_commands(profile)
