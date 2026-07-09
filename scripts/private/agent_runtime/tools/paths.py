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
REVIEW_PACKET_PREFIX = ".github/agent-runtime/review/out/review-packet/"
REVIEW_PACKET_DIR = REVIEW_PACKET_PREFIX.rstrip("/")
HIDDEN_REVIEW_PATH_PREFIXES = (
    ".agent-runtime/",
    ".github/agent-runtime/review/out/",
    "artifacts/",
    "datasets/",
    "deps/",
    "development/build/",
    "tmp/",
    "_playwright_pages_site/",
    "_yolo_benchmark_pages_site/",
    "_yolo_performance_dataset_cache/",
)
PATCH_PATH_PREFIXES = ("a/", "b/")
PATCH_FILE_HEADER_PREFIXES = ("--- ", "+++ ")
PATCH_MOVE_HEADER_PREFIXES = ("rename from ", "rename to ", "copy from ", "copy to ")
PATCH_QUOTE_CHARS = ('"', "'")


def is_git_metadata_path(relative_path: str) -> bool:
    return (
        relative_path == GIT_METADATA_DIR
        or relative_path.startswith(GIT_METADATA_PREFIX)
        or f"/{GIT_METADATA_PREFIX}" in relative_path
        or relative_path.endswith(f"/{GIT_METADATA_DIR}")
    )


def normalize_relative_path(relative_path: str) -> str:
    return relative_path.removeprefix("./")


def is_review_packet_path(relative_path: str) -> bool:
    path = normalize_relative_path(relative_path)
    return path == REVIEW_PACKET_DIR or path.startswith(REVIEW_PACKET_PREFIX)


def is_hidden_review_path(relative_path: str) -> bool:
    path = normalize_relative_path(relative_path)
    if is_review_packet_path(path):
        return False
    return any(path == prefix.rstrip("/") or path.startswith(prefix) for prefix in HIDDEN_REVIEW_PATH_PREFIXES)


def is_hidden_review_glob(pattern: str) -> bool:
    path = normalize_relative_path(pattern).rstrip("*")
    if is_review_packet_path(path):
        return False
    return any(path == prefix.rstrip("/") or path.startswith(prefix) for prefix in HIDDEN_REVIEW_PATH_PREFIXES)


def reject_hidden_review_path(relative_path: str, operation: str) -> None:
    if is_hidden_review_path(relative_path):
        raise ValueError(
            f"{operation} path targets hidden review runtime/generated output: {relative_path}. "
            f"Use {REVIEW_PACKET_DIR}/ for review packet evidence or inspect repository source files."
        )


def resolve_safe_repo_path(context: AgentRunContext, path_value: str, operation: str) -> Path:
    path = context.resolve_repo_path(path_value)
    relative = path.relative_to(context.repo_root).as_posix()
    if is_git_metadata_path(relative):
        raise ValueError(f"{operation} path targets git metadata: {path_value}")
    return path


def resolve_mutable_repo_path(context: AgentRunContext, path_value: str, operation: str) -> Path:
    return resolve_safe_repo_path(context, path_value, operation)


def resolve_stdin_redirection_path(context: AgentRunContext, path_value: str) -> Path:
    return resolve_safe_repo_path(context, path_value, "Shell stdin redirection")


def parse_patch_path(path_value: str) -> str:
    if path_value.startswith(PATCH_QUOTE_CHARS):
        if "\\" in path_value:
            raise ValueError(f"Escaped patch paths are not supported: {path_value}")
        try:
            words = shlex.split(path_value)
        except ValueError as exc:
            raise ValueError(f"Unable to parse quoted patch path: {path_value}") from exc
        if len(words) != 1:
            raise ValueError(f"Unsupported quoted patch path: {path_value}")
        return words[0]
    return path_value


def normalize_patch_path(path_value: str) -> str | None:
    path_value = parse_patch_path(path_value)
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
                normalized = normalize_patch_path(line[len(prefix):])
                if normalized:
                    paths.append(normalized)
                break
    return sorted(set(paths))


def validate_patch_paths(context: AgentRunContext, patch: str) -> None:
    paths = patch_header_paths(patch)
    if not paths:
        raise ValueError("Patch does not contain any file paths.")
    for path in paths:
        resolve_mutable_repo_path(context, path, "Patch")
