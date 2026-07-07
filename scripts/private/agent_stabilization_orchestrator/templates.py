#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from pathlib import Path

from .paths import MARKDOWN_TEMPLATE_ROOT, POLICY_MARKDOWN_TEMPLATE_ROOT
from .profile import profile_string, profile_string_list, profile_validation_commands


DISPLAY_NAME_TOKEN = "{{DISPLAY_NAME}}"
PROMPT_CONTEXT_FILES_TOKEN = "{{PROMPT_CONTEXT_FILES}}"
VALIDATION_COMMANDS_TOKEN = "{{VALIDATION_COMMANDS}}"
CONTEXT_ROOT_TOKEN = "{{CONTEXT_ROOT}}"
PR_NUMBER_TOKEN = "{{PR_NUMBER}}"
HEAD_BRANCH_TOKEN = "{{HEAD_BRANCH}}"
REVIEW_RECOMMENDATION_TOKEN = "{{REVIEW_RECOMMENDATION}}"
REVIEW_RUN_ID_TOKEN = "{{REVIEW_RUN_ID}}"
REVIEW_STATE_JSON_TOKEN = "{{REVIEW_STATE_JSON}}"
REVIEW_SUMMARY_TOKEN = "{{REVIEW_SUMMARY}}"
REVIEW_WORKFLOW_NAME_TOKEN = "{{REVIEW_WORKFLOW_NAME}}"
SOURCE_RUN_ID_TOKEN = "{{SOURCE_RUN_ID}}"


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
