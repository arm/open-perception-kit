#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import json
import os
import re
import shlex
import subprocess
from pathlib import Path

from agent_runtime.contracts import (
    AgentInstance,
    load_json_object,
    require_list,
    require_non_empty_string,
)
from agent_runtime.config.model import resolve_agent_model

TASK_REF_PATTERN = r"[A-Z][A-Z0-9]*-[0-9]+"
TASK_REF_RE = re.compile(rf"^{TASK_REF_PATTERN}$")
TASK_REF_SCAN_RE = re.compile(rf"(?<![A-Z0-9])({TASK_REF_PATTERN})(?![A-Z0-9])")
GITHUB_WORKSPACE = os.environ.get("GITHUB_WORKSPACE", "").strip()
HELPER_ROOT = Path(__file__).resolve().parents[3]
REPO_ROOT = Path(GITHUB_WORKSPACE).resolve() if GITHUB_WORKSPACE else HELPER_ROOT
DEFAULT_PROFILE_PATH = REPO_ROOT / ".github/agent-runtime/workflow-action-update-agent/profiles/profile.json"
PR_TEMPLATE_PATH = REPO_ROOT / ".github/PULL_REQUEST_TEMPLATE.md"
MARKDOWN_TEMPLATE_ROOT = HELPER_ROOT / ".github/agent-runtime/workflow-action-update-agent/prompts"
PR_AUTOMATION_START = "<!-- workflow-action-update-agent:automation:start -->"
PR_AUTOMATION_END = "<!-- workflow-action-update-agent:automation:end -->"
PR_DESCRIPTION_START = "<!-- workflow-action-update-agent:description:start -->"
PR_DESCRIPTION_END = "<!-- workflow-action-update-agent:description:end -->"
DISPLAY_NAME_TOKEN = "{{DISPLAY_NAME}}"
PROMPT_CONTEXT_FILES_TOKEN = "{{PROMPT_CONTEXT_FILES}}"
VALIDATION_COMMANDS_TOKEN = "{{VALIDATION_COMMANDS}}"
CONTEXT_ROOT_TOKEN = "{{CONTEXT_ROOT}}"
PR_NUMBER_TOKEN = "{{PR_NUMBER}}"
REPAIR_BRANCH_TOKEN = "{{REPAIR_BRANCH}}"
REPAIR_DEFINITION_OF_DONE_TOKEN = "{{REPAIR_DEFINITION_OF_DONE}}"
REVIEW_RECOMMENDATION_TOKEN = "{{REVIEW_RECOMMENDATION}}"
REVIEW_RUN_ID_TOKEN = "{{REVIEW_RUN_ID}}"
REVIEW_STATE_JSON_TOKEN = "{{REVIEW_STATE_JSON}}"
REVIEW_SUMMARY_TOKEN = "{{REVIEW_SUMMARY}}"
REVIEW_WORKFLOW_NAME_TOKEN = "{{REVIEW_WORKFLOW_NAME}}"
SOURCE_RUN_ID_TOKEN = "{{SOURCE_RUN_ID}}"
SOURCE_PR_NUMBER_TOKEN = "{{SOURCE_PR_NUMBER}}"
SOURCE_RUN_URL_TOKEN = "{{SOURCE_RUN_URL}}"
SOURCE_WORKFLOW_NAME_TOKEN = "{{SOURCE_WORKFLOW_NAME}}"
TARGET_BRANCH_TOKEN = "{{TARGET_BRANCH}}"
TASK_REF_TOKEN = "{{TASK_REF}}"
REPAIR_AUTHORIZATION_LABEL_TOKEN = "{{REPAIR_AUTHORIZATION_LABEL}}"
WAIT_TIMEOUT_SECONDS = 1800
AGENT_WORKFLOW_VALIDATION_COMMAND_SET = "agent-workflow-python"
CANONICAL_VALIDATION_COMMAND_SETS = {
    AGENT_WORKFLOW_VALIDATION_COMMAND_SET: (
        "python3 -m unittest discover -s scripts/private/tests",
        "python3 -m unittest discover -s scripts/private/agent_runtime/tests",
        "python3 -m unittest discover -s scripts/private/workflow_action_update_agent/tests",
        "python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_agent_static_analysis.py'",
        "python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_detect_secrets_quality_flow.py'",
        "python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_agent_workflow_contracts.py'",
        "git diff --stat",
    ),
}
STANDARD_AGENT_REVIEW_WORKFLOW: dict[str, str] = {
    "workflow_file": "agent-review.yml",
    "workflow_name": "Agent Review",
    "review_state_script": "scripts/private/agent_runtime/review/fetch.py",
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


def run_command(
    args: list[str],
    *,
    capture_output: bool = False,
    check: bool = True,
    env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        check=check,
        text=True,
        capture_output=capture_output,
        env=env,
    )


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


def validation_command_environment() -> dict[str, str]:
    blocked_names = set(VALIDATION_ENV_BLOCKLIST)
    return {
        key: value
        for key, value in os.environ.items()
        if key.upper() not in blocked_names
        and not any(fragment in key.upper() for fragment in VALIDATION_ENV_SENSITIVE_FRAGMENTS)
    }


def task_refs_from_text(value: str) -> list[str]:
    text = value.strip()
    if TASK_REF_RE.fullmatch(text):
        return [text]
    return TASK_REF_SCAN_RE.findall(text)


def resolve_task_ref(*values: str, purpose: str) -> str:
    refs: list[str] = []
    for value in values:
        refs.extend(task_refs_from_text(str(value or "")))

    unique_refs = sorted(set(refs))
    if not unique_refs:
        raise ValueError(f"{purpose} requires a task reference matching PROJECT-1234.")
    if len(unique_refs) > 1:
        raise ValueError(
            f"{purpose} found conflicting task references: "
            + ", ".join(unique_refs)
        )
    return unique_refs[0]


def write_outputs(values: dict[str, str], output_path: str | None = None) -> None:
    target = output_path or os.environ.get("GITHUB_OUTPUT")
    if not target:
        raise ValueError("GITHUB_OUTPUT is not set and no explicit output path was provided.")
    with Path(target).open("a", encoding="utf-8") as output_file:
        for key, value in values.items():
            output_file.write(f"{key}={value}\n")


def resolve_repo_path(path_value: str) -> Path:
    path = Path(path_value)
    if path.is_absolute():
        return path
    return (REPO_ROOT / path).resolve()


def default_profile_path_argument() -> str:
    try:
        return str(DEFAULT_PROFILE_PATH.relative_to(REPO_ROOT))
    except ValueError:
        return str(DEFAULT_PROFILE_PATH)


def profile_config_root(profile_path: str) -> Path:
    path = resolve_repo_path(profile_path or default_profile_path_argument())
    try:
        relative_parts = path.relative_to(REPO_ROOT).parts
    except ValueError:
        return path.parent

    marker_parts = Path(".github/agent-runtime/workflow-action-update-agent/profiles").parts
    for index in range(0, len(relative_parts) - len(marker_parts) + 1):
        if relative_parts[index:index + len(marker_parts)] == marker_parts:
            return REPO_ROOT.joinpath(*relative_parts[:index]).resolve()
    return REPO_ROOT


def write_json_file(path: Path, payload: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


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


def profile_agent_model(profile: dict[str, object], agent_instance: AgentInstance, profile_path: str = "") -> str:
    model_config_path = Path(profile_string(profile, "agent_model_config"))
    if not model_config_path.is_absolute():
        model_config_path = profile_config_root(profile_path) / model_config_path
    return resolve_agent_model(
        model_config_path,
        agent_instance,
    )


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


def standard_agent_review_workflow() -> dict[str, str]:
    return dict(STANDARD_AGENT_REVIEW_WORKFLOW)


def format_profile_template(template: str, values: dict[str, str]) -> str:
    try:
        return template.format_map(values)
    except KeyError as exc:
        missing_key = exc.args[0]
        raise ValueError(f"Profile template is missing a value for '{missing_key}'.") from exc


def profile_markdown_list(profile: dict[str, object], key: str) -> str:
    return "\n".join(f"- `{item}`" for item in profile_string_list(profile, key))


def markdown_list(items: list[str]) -> str:
    return "\n".join(f"- `{item}`" for item in items)


def profile_prompt_replacements(profile: dict[str, object]) -> dict[str, str]:
    return {
        DISPLAY_NAME_TOKEN: profile_string(profile, "display_name"),
        PROMPT_CONTEXT_FILES_TOKEN: profile_markdown_list(profile, "prompt_context_files"),
        VALIDATION_COMMANDS_TOKEN: markdown_list(profile_validation_commands(profile)),
    }


def load_markdown_template(name: str) -> str:
    template_path = MARKDOWN_TEMPLATE_ROOT / name
    if not template_path.is_file():
        raise ValueError(f"Markdown template file is missing: {template_path}")
    return template_path.read_text(encoding="utf-8")


def render_markdown_template(name: str, replacements: dict[str, str]) -> str:
    template = load_markdown_template(name)
    for token, replacement in replacements.items():
        template = template.replace(token, replacement)
    return template
