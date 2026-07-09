#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class AgentRunContext:
    repo_root: Path
    command_timeout: int

    def __post_init__(self) -> None:
        object.__setattr__(self, "repo_root", self.repo_root.resolve())

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


def activate_run_context(context: AgentRunContext) -> AgentRunContext:
    global RUN_CONTEXT
    RUN_CONTEXT = context
    return context


def set_run_context(repo_root: Path, command_timeout: int) -> AgentRunContext:
    return activate_run_context(AgentRunContext(repo_root, command_timeout))


def require_run_context() -> AgentRunContext:
    if RUN_CONTEXT is None:
        raise RuntimeError("Agent run context has not been configured.")
    return RUN_CONTEXT
