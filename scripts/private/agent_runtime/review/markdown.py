#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import urllib.parse

from ..contracts import (
    DiffSide,
    MARKER,
    RECOMMENDATION_COLORS,
    SEVERITY_COLORS,
    STATE_MARKER,
    ReviewSeverity,
)
from .markers import review_state_marker, review_state_metadata
from .values import required_float

BADGE_LABEL_COLOR = "202938"


def format_location(finding: dict[str, object]) -> str:
    start_line = finding.get("start_line")
    end_line = finding.get("end_line")
    path = str(finding["path"])
    diff_side = finding.get("diff_side")
    side_suffix = f" ({diff_side})" if diff_side in {DiffSide.LEFT.value, DiffSide.RIGHT.value} else ""
    if start_line is None:
        return f"{path}{side_suffix}"
    if end_line is None or end_line == start_line:
        return f"{path}:L{start_line}{side_suffix}"
    return f"{path}:L{start_line}-L{end_line}{side_suffix}"


def make_badge(label: str, message: object, color: str) -> str:
    label_text = urllib.parse.quote(str(label), safe="")
    message_text = urllib.parse.quote(str(message), safe="")
    return (
        f"![{label}: {message}]"
        f"(https://img.shields.io/badge/{label_text}-{message_text}-{color}"
        f"?style=flat&labelColor={BADGE_LABEL_COLOR})"
    )


def severity_badge(severity: str) -> str:
    color = SEVERITY_COLORS.get(severity)
    if color is None:
        return f"**{severity.upper()}**"
    return make_badge("severity", severity.upper(), color)


def severity_count_badge(severity: str, count: int) -> str:
    color = SEVERITY_COLORS.get(severity)
    if color is None:
        return f"**{severity.upper()}: {count}**"
    return make_badge(severity.upper(), count, color)


def recommendation_badge(recommendation: str) -> str:
    color = RECOMMENDATION_COLORS.get(recommendation)
    if color is None:
        return f"**{recommendation}**"
    return make_badge("recommendation", recommendation.replace("_", " ").upper(), color)


def score_badge(score: float) -> str:
    if score >= 0.85:
        color = "dc2626"
    elif score >= 0.60:
        color = "d97706"
    elif score >= 0.30:
        color = "2563eb"
    else:
        color = "15803d"
    return make_badge("score", f"{score:.2f}", color)


def confidence_badge(confidence: float) -> str:
    if confidence >= 0.85:
        color = "15803d"
    elif confidence >= 0.60:
        color = "2563eb"
    elif confidence >= 0.30:
        color = "d97706"
    else:
        color = "dc2626"
    return make_badge("confidence", f"{confidence:.2f}", color)


def summarize_findings(findings: list[dict[str, object]]) -> dict[str, int]:
    counts = {severity.value: 0 for severity in ReviewSeverity}
    for finding in findings:
        severity = finding.get("severity")
        if severity in counts:
            counts[str(severity)] += 1
    return counts


def format_finding_details(findings: list[dict[str, object]]) -> list[str]:
    if not findings:
        return ["No findings."]

    lines: list[str] = []
    for index, finding in enumerate(findings, start=1):
        lines.extend(
            [
                f"{index}. {severity_badge(str(finding['severity']))} **{finding['title']}**",
                f"   Location: `{format_location(finding)}`",
                f"   Score: `{required_float(finding['score'], 'score'):.2f}` "
                f"Confidence: `{required_float(finding['confidence'], 'confidence'):.2f}`",
                f"   {finding['body']}",
            ]
        )
        suggestion = finding.get("suggestion")
        if suggestion:
            lines.extend(
                [
                    "   Suggested change:",
                    "",
                    "   ```text",
                    *[f"   {line}" for line in str(suggestion).rstrip("\n").splitlines()],
                    "   ```",
                ]
            )
        lines.append("")
    while lines and lines[-1] == "":
        lines.pop()
    return lines


def format_markdown(review: dict[str, object], *, run_id: str = "", head_sha: str = "") -> str:
    findings = review.get("findings", [])
    if not isinstance(findings, list):
        findings = []
    typed_findings = [finding for finding in findings if isinstance(finding, dict)]
    counts = summarize_findings(typed_findings)
    finding_badges = " ".join(
        [
            severity_count_badge("critical", counts["critical"]),
            severity_count_badge("major", counts["major"]),
            severity_count_badge("note", counts["note"]),
        ]
    )
    lines = [
        MARKER,
        review_state_marker(review_state_metadata(review, run_id, head_sha), STATE_MARKER),
        "## Agent Review",
        "",
        recommendation_badge(str(review["overall_recommendation"])),
        f"{score_badge(required_float(review['overall_score'], 'overall_score'))} "
        f"{confidence_badge(required_float(review['overall_confidence'], 'overall_confidence'))}",
        "",
        f"Findings: {finding_badges}",
        "",
        str(review["summary"]),
        "",
        "### Findings",
        "",
        *format_finding_details(typed_findings),
    ]

    return "\n".join(lines).rstrip() + "\n"
