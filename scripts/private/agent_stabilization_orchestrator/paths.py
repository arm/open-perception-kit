#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import os
from pathlib import Path


GITHUB_WORKSPACE = os.environ.get("GITHUB_WORKSPACE", "").strip()
SOURCE_TREE_ROOT = Path(__file__).resolve().parents[3]
REPO_ROOT = Path(GITHUB_WORKSPACE).resolve() if GITHUB_WORKSPACE else SOURCE_TREE_ROOT
PROFILE_ROOT = Path(".github/agent-runtime/pr-stabilization/profiles")
PROMPT_ROOT = Path(".github/agent-runtime/pr-stabilization/prompts")
POLICY_PROMPT_ROOT = Path(".github/agent-runtime/workflow-policy")
DEFAULT_PROFILE_PATH = REPO_ROOT / PROFILE_ROOT / "profile.json"
MARKDOWN_TEMPLATE_ROOT = SOURCE_TREE_ROOT / PROMPT_ROOT
POLICY_MARKDOWN_TEMPLATE_ROOT = SOURCE_TREE_ROOT / POLICY_PROMPT_ROOT


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

    marker_parts = PROFILE_ROOT.parts
    for index in range(0, len(relative_parts) - len(marker_parts) + 1):
        if relative_parts[index:index + len(marker_parts)] == marker_parts:
            return REPO_ROOT.joinpath(*relative_parts[:index]).resolve()
    return REPO_ROOT
