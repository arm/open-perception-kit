#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from .paths import MARKDOWN_TEMPLATE_ROOT
from .profile import profile_string, profile_string_list, profile_validation_commands


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
