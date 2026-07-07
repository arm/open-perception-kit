#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from pathlib import Path


class AgentRunContext:
    def __init__(self, repo_root: Path, command_timeout: int) -> None:
        self.repo_root = repo_root.resolve()
        self.command_timeout = command_timeout

    def resolve_repo_path(self, path_value: str) -> Path:
        path = Path(path_value)
        if path.is_absolute():
            resolved = path.resolve()
        else:
            resolved = (self.repo_root / path).resolve()
        if resolved != self.repo_root and self.repo_root not in resolved.parents:
            raise ValueError(f"Path escapes repository root: {path_value}")
        return resolved


RUN_CONTEXT: AgentRunContext | None = None


def set_run_context(repo_root: Path, command_timeout: int) -> AgentRunContext:
    global RUN_CONTEXT
    RUN_CONTEXT = AgentRunContext(repo_root, command_timeout)
    return RUN_CONTEXT


def require_run_context() -> AgentRunContext:
    if RUN_CONTEXT is None:
        raise RuntimeError("Agent run context has not been configured.")
    return RUN_CONTEXT
