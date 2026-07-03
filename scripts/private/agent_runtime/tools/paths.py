#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from pathlib import Path
import shlex

from ..runtime_context import AgentRunContext

GIT_METADATA_DIR = ".git"
GIT_METADATA_PREFIX = f"{GIT_METADATA_DIR}/"
PATCH_PATH_PREFIXES = ("a/", "b/")
PATCH_FILE_HEADER_PREFIXES = ("--- ", "+++ ")
PATCH_MOVE_HEADER_PREFIXES = ("rename from ", "rename to ", "copy from ", "copy to ")


def resolve_safe_repo_path(context: AgentRunContext, path_value: str, operation: str) -> Path:
    path = context.resolve_repo_path(path_value)
    relative = path.relative_to(context.repo_root).as_posix()
    if relative == GIT_METADATA_DIR or relative.startswith(GIT_METADATA_PREFIX):
        raise ValueError(f"{operation} path targets git metadata: {path_value}")
    return path


def resolve_mutable_repo_path(context: AgentRunContext, path_value: str, operation: str) -> Path:
    return resolve_safe_repo_path(context, path_value, operation)


def resolve_redirection_path(context: AgentRunContext, path_value: str) -> Path:
    return resolve_mutable_repo_path(context, path_value, "Shell redirection")


def normalize_patch_path(path_value: str) -> str | None:
    if path_value == "/dev/null":
        return None
    for prefix in PATCH_PATH_PREFIXES:
        if path_value.startswith(prefix):
            return path_value[len(prefix):]
    return path_value


def patch_header_paths(patch: str) -> list[str]:
    paths: list[str] = []
    for line in patch.splitlines():
        if line.startswith("diff --git "):
            try:
                words = shlex.split(line)
            except ValueError as exc:
                raise ValueError(f"Unable to parse patch file header: {line}") from exc
            if len(words) != 4:
                raise ValueError(f"Unsupported patch file header: {line}")
            paths.extend(path for path in (normalize_patch_path(words[2]), normalize_patch_path(words[3])) if path)
            continue
        for prefix in PATCH_FILE_HEADER_PREFIXES:
            if line.startswith(prefix):
                path_text = line[len(prefix):].split("\t", 1)[0]
                normalized = normalize_patch_path(path_text)
                if normalized:
                    paths.append(normalized)
                break
        for prefix in PATCH_MOVE_HEADER_PREFIXES:
            if line.startswith(prefix):
                paths.append(line[len(prefix):])
                break
    return sorted(set(paths))


def validate_patch_paths(context: AgentRunContext, patch: str) -> None:
    paths = patch_header_paths(patch)
    if not paths:
        raise ValueError("Patch does not contain any file paths.")
    for path in paths:
        resolve_mutable_repo_path(context, path, "Patch")
