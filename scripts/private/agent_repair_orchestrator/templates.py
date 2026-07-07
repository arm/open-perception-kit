#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from pathlib import Path

from .paths import MARKDOWN_TEMPLATE_ROOT, POLICY_MARKDOWN_TEMPLATE_ROOT
from .profile import profile_string, profile_string_list, profile_validation_commands


PR_AUTOMATION_START = "<!-- agent-repair:automation:start -->"
PR_AUTOMATION_END = "<!-- agent-repair:automation:end -->"
PR_DESCRIPTION_START = "<!-- agent-repair:description:start -->"
PR_DESCRIPTION_END = "<!-- agent-repair:description:end -->"
DISPLAY_NAME_TOKEN = "{{DISPLAY_NAME}}"
PROMPT_CONTEXT_FILES_TOKEN = "{{PROMPT_CONTEXT_FILES}}"
VALIDATION_COMMANDS_TOKEN = "{{VALIDATION_COMMANDS}}"
CONTEXT_ROOT_TOKEN = "{{CONTEXT_ROOT}}"
REPAIR_BRANCH_TOKEN = "{{REPAIR_BRANCH}}"
REPAIR_DEFINITION_OF_DONE_TOKEN = "{{REPAIR_DEFINITION_OF_DONE}}"
SOURCE_RUN_ID_TOKEN = "{{SOURCE_RUN_ID}}"
SOURCE_PR_NUMBER_TOKEN = "{{SOURCE_PR_NUMBER}}"
SOURCE_RUN_URL_TOKEN = "{{SOURCE_RUN_URL}}"
SOURCE_WORKFLOW_NAME_TOKEN = "{{SOURCE_WORKFLOW_NAME}}"
TARGET_BRANCH_TOKEN = "{{TARGET_BRANCH}}"
TASK_REF_TOKEN = "{{TASK_REF}}"
REPAIR_AUTHORIZATION_LABEL_TOKEN = "{{REPAIR_AUTHORIZATION_LABEL}}"


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
        REPAIR_DEFINITION_OF_DONE_TOKEN: profile_markdown_list(profile, "repair_definition_of_done"),
        REPAIR_AUTHORIZATION_LABEL_TOKEN: profile_string(profile, "repair_authorization_label"),
    }


def load_markdown_template(name: str) -> str:
    template_path = _template_path(name)
    if not template_path.is_file():
        raise ValueError(f"Markdown template file is missing: {template_path}")
    return template_path.read_text(encoding="utf-8")


def _template_path(name: str) -> Path:
    policy_path = POLICY_MARKDOWN_TEMPLATE_ROOT / name
    if policy_path.is_file():
        return policy_path
    return MARKDOWN_TEMPLATE_ROOT / name


def render_markdown_template(name: str, replacements: dict[str, str]) -> str:
    template = load_markdown_template(name)
    for token, replacement in replacements.items():
        template = template.replace(token, replacement)
    return template
