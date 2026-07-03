#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import os
import re
import subprocess
import sys

from ..contracts import DiffSide
from .values import required_int

DIFF_HUNK_RE = re.compile(
    r"^@@ -(?P<old_start>\d+)(?:,(?P<old_count>\d+))? "
    r"\+(?P<new_start>\d+)(?:,(?P<new_count>\d+))? @@"
)


def diff_path_from_header(value: str) -> str | None:
    if value == "/dev/null":
        return None
    if value.startswith("a/") or value.startswith("b/"):
        return value[2:]
    return value


def parse_diff_comment_anchors(diff_text: str) -> set[tuple[str, str, int]]:
    anchors: set[tuple[str, str, int]] = set()
    old_path = None
    new_path = None
    old_line = None
    new_line = None

    for line in diff_text.splitlines():
        if line.startswith("diff --git "):
            old_path = None
            new_path = None
            old_line = None
            new_line = None
            continue
        if line.startswith("--- "):
            old_path = diff_path_from_header(line[4:].split("\t", 1)[0])
            old_line = None
            new_line = None
            continue
        if line.startswith("+++ "):
            new_path = diff_path_from_header(line[4:].split("\t", 1)[0])
            old_line = None
            new_line = None
            continue
        match = DIFF_HUNK_RE.match(line)
        if match:
            old_line = int(match.group("old_start"))
            new_line = int(match.group("new_start"))
            continue
        if old_line is None or new_line is None:
            continue
        if line.startswith("\\"):
            continue
        old_anchor_path = old_path or new_path
        new_anchor_path = new_path or old_path
        if line.startswith("-"):
            if old_anchor_path is not None:
                anchors.add((old_anchor_path, DiffSide.LEFT.value, old_line))
            old_line += 1
            continue
        if line.startswith("+"):
            if new_anchor_path is not None:
                anchors.add((new_anchor_path, DiffSide.RIGHT.value, new_line))
            new_line += 1
            continue
        if line.startswith(" "):
            if new_anchor_path is not None:
                anchors.add((new_anchor_path, DiffSide.RIGHT.value, new_line))
            old_line += 1
            new_line += 1

    return anchors


def review_base_ref_from_env() -> str:
    review_base_ref = os.environ.get("REVIEW_BASE_REF")
    if review_base_ref:
        return review_base_ref
    github_base_ref = os.environ.get("GITHUB_BASE_REF")
    if github_base_ref:
        return f"origin/{github_base_ref}"
    return ""


def build_diff_comment_anchors(base_ref: str, head_ref: str = "HEAD") -> set[tuple[str, str, int]] | None:
    if not base_ref:
        return None
    try:
        completed = subprocess.run(
            ["git", "diff", "--no-ext-diff", f"{base_ref}...{head_ref}"],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        print(
            f"Failed to build Agent review diff anchors from {base_ref}...{head_ref}: {exc}",
            file=sys.stderr,
        )
        return None
    return parse_diff_comment_anchors(completed.stdout)


def is_review_comment_payload_anchored(
    comment: dict[str, object],
    diff_anchors: set[tuple[str, str, int]],
) -> bool:
    path = str(comment["path"])
    side = str(comment["side"])
    line = required_int(comment["line"], "line")
    if "start_line" not in comment:
        return (path, side, line) in diff_anchors
    start_line = required_int(comment["start_line"], "start_line")
    start_side = str(comment["start_side"])
    if start_side != side or start_line > line:
        return False
    return all(
        (path, side, candidate) in diff_anchors
        for candidate in range(start_line, line + 1)
    )


def filter_review_comment_payloads_for_diff(
    comments: list[dict[str, object]],
    diff_anchors: set[tuple[str, str, int]] | None,
) -> list[dict[str, object]]:
    if diff_anchors is None:
        return comments
    return [
        comment
        for comment in comments
        if is_review_comment_payload_anchored(comment, diff_anchors)
    ]
