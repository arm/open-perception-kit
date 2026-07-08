#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import re


TASK_REF_PATTERN = r"[A-Z][A-Z0-9]*-[0-9]+"
TASK_REF_RE = re.compile(rf"^{TASK_REF_PATTERN}$")
TASK_REF_SCAN_RE = re.compile(rf"(?<![A-Z0-9])({TASK_REF_PATTERN})(?![A-Z0-9])")


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
