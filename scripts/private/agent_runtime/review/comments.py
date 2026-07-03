#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from ..contracts import DiffSide, INLINE_MARKER
from .markdown import confidence_badge, format_location, score_badge, severity_badge
from .values import required_float, required_int

MAX_INLINE_SUGGESTION_LINES = 10


def review_diff_side(finding: dict[str, object]) -> str:
    diff_side = finding.get("diff_side")
    if diff_side in {DiffSide.LEFT.value, DiffSide.RIGHT.value}:
        return str(diff_side)
    return DiffSide.RIGHT.value


def is_inline_suggestion_applicable(finding: dict[str, object]) -> bool:
    suggestion = finding.get("suggestion")
    start_line = finding.get("start_line")
    end_line = finding.get("end_line")
    if not (
        suggestion
        and finding.get("path")
        and start_line is not None
        and review_diff_side(finding) == DiffSide.RIGHT.value
    ):
        return False

    start_line_number = required_int(start_line, "start_line")
    effective_end_line = end_line if end_line is not None else start_line
    touched_lines = (required_int(effective_end_line, "end_line") - start_line_number) + 1
    suggestion_lines = len(str(suggestion).rstrip("\n").splitlines())

    return bool(
        touched_lines <= MAX_INLINE_SUGGESTION_LINES
        and suggestion_lines <= MAX_INLINE_SUGGESTION_LINES
    )


def is_location_comment_applicable(finding: dict[str, object]) -> bool:
    return bool(
        finding.get("path")
        and finding.get("start_line") is not None
        and finding.get("diff_side") in {DiffSide.LEFT.value, DiffSide.RIGHT.value}
    )


def build_inline_comment_body(finding: dict[str, object], *, run_id: str) -> str:
    suggestion = finding.get("suggestion")
    use_inline_block = is_inline_suggestion_applicable(finding)
    finding_badges = " ".join(
        [
            severity_badge(str(finding["severity"])),
            score_badge(required_float(finding["score"], "score")),
            confidence_badge(required_float(finding["confidence"], "confidence")),
        ]
    )
    lines = [
        INLINE_MARKER,
        f"{finding_badges} **{finding['title']}**",
        "",
        f"Location: `{format_location(finding)}`",
        "",
        str(finding["body"]),
    ]

    if suggestion:
        if use_inline_block:
            lines.extend(
                [
                    "",
                    "```suggestion",
                    str(suggestion).rstrip("\n"),
                    "```",
                ]
            )
        else:
            lines.extend(
                [
                    "",
                    "Suggested change:",
                    "",
                    "```text",
                    str(suggestion).rstrip("\n"),
                    "```",
                ]
            )

    return "\n".join(lines) + "\n"


def build_review_comment_payload(finding: dict[str, object], *, run_id: str) -> dict[str, object]:
    side = review_diff_side(finding)
    payload: dict[str, object] = {
        "body": build_inline_comment_body(finding, run_id=run_id),
        "path": finding["path"],
        "line": finding["end_line"]
        if finding.get("end_line") is not None
        else finding["start_line"],
        "side": side,
    }
    # A single invalid anchor rejects the whole batched review. Keep deleted
    # LEFT-side findings on one stable deleted line instead of risking a range.
    if (
        side == DiffSide.RIGHT.value
        and finding.get("end_line") is not None
        and finding["end_line"] != finding["start_line"]
    ):
        payload["start_line"] = finding["start_line"]
        payload["start_side"] = side
    return payload


def build_review_comment_payloads(
    findings: list[dict[str, object]],
    *,
    run_id: str,
    diff_anchors: set[tuple[str, str, int]] | None = None,
) -> list[dict[str, object]]:
    from .diff_anchors import filter_review_comment_payloads_for_diff

    comments = []
    for finding in findings:
        if not is_location_comment_applicable(finding):
            continue
        comments.append(build_review_comment_payload(finding, run_id=run_id))
    return filter_review_comment_payloads_for_diff(comments, diff_anchors)
