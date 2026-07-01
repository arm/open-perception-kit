#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from pathlib import Path
import sys
from typing import Any

if __package__ in (None, ""):  # pragma: no cover - used for direct script imports.
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    __package__ = "agent_runtime"

from .contracts import (
    DiffSide,
    ReviewRecommendation,
    ReviewSeverity,
    UNSUPPORTED_REVIEW_CLAIM_GUARDS,
)


def filter_invalid_right_side_findings(payload: dict[str, Any], repo_root: Path) -> dict[str, Any]:
    """Drop findings that claim RIGHT-side anchors absent from the checkout."""

    findings = payload.get("findings")
    if not isinstance(findings, list):
        return payload

    kept_findings: list[dict[str, Any]] = []
    dropped_count = 0
    for finding in findings:
        if isinstance(finding, dict) and _has_invalid_right_side_anchor(finding, repo_root):
            dropped_count += 1
            continue
        kept_findings.append(finding)

    if dropped_count == 0:
        return payload

    filtered = dict(payload)
    filtered["findings"] = kept_findings
    filtered["overall_recommendation"] = _recommendation_for_findings(kept_findings)
    filtered["overall_score"] = max(
        (float(finding.get("score", 0.0)) for finding in kept_findings if isinstance(finding, dict)),
        default=0.0,
    )
    filtered["summary"] = _summary_for_filtered_findings(kept_findings, dropped_count)
    return filtered


def _has_invalid_right_side_anchor(finding: dict[str, Any], repo_root: Path) -> bool:
    if finding.get("diff_side") != DiffSide.RIGHT.value:
        return False

    path = finding.get("path")
    if not isinstance(path, str) or not path:
        return True

    try:
        file_path = _resolve_repo_path(repo_root, path)
    except ValueError:
        return True
    if not file_path.is_file():
        return True

    lines = file_path.read_text(encoding="utf-8", errors="replace").splitlines()
    line_count = len(lines)
    start_line = finding.get("start_line")
    end_line = finding.get("end_line")
    if isinstance(start_line, int) and start_line > line_count:
        return True
    if isinstance(end_line, int) and end_line > line_count:
        return True
    if isinstance(start_line, int) and isinstance(end_line, int) and end_line < start_line:
        return True

    finding_text = _finding_text(finding)
    anchor_text = _anchor_text(lines, start_line, end_line)
    return any(
        guard.matches(finding_text=finding_text, anchor_text=anchor_text)
        for guard in UNSUPPORTED_REVIEW_CLAIM_GUARDS
    )


def _resolve_repo_path(repo_root: Path, path_value: str) -> Path:
    path = Path(path_value)
    resolved = path.resolve() if path.is_absolute() else (repo_root / path).resolve()
    root = repo_root.resolve()
    if resolved != root and root not in resolved.parents:
        raise ValueError(f"Path escapes repository root: {path_value}")
    return resolved


def _finding_text(finding: dict[str, Any]) -> str:
    return "\n".join(
        str(finding.get(field) or "")
        for field in ("title", "body", "suggestion")
    )


def _anchor_text(lines: list[str], start_line: Any, end_line: Any) -> str:
    if not isinstance(start_line, int):
        return "\n".join(lines)
    if not isinstance(end_line, int):
        end_line = start_line
    first = max(start_line - 1, 0)
    last = max(end_line, start_line)
    return "\n".join(lines[first:last])


def _recommendation_for_findings(findings: list[dict[str, Any]]) -> str:
    severities = {
        str(finding.get("severity", ""))
        for finding in findings
        if isinstance(finding, dict)
    }
    blocking_severities = {ReviewSeverity.MAJOR.value, ReviewSeverity.CRITICAL.value}
    if severities.intersection(blocking_severities):
        return ReviewRecommendation.REQUEST_CHANGES.value
    if findings:
        return ReviewRecommendation.COMMENT.value
    return ReviewRecommendation.APPROVE.value


def _summary_for_filtered_findings(findings: list[dict[str, Any]], dropped_count: int) -> str:
    suffix = (
        f"Omitted {dropped_count} unsupported RIGHT-side finding"
        f"{'' if dropped_count == 1 else 's'} whose anchors are not supported"
        " by the current checkout."
    )
    if not findings:
        return f"No supported findings remain after filtering. {suffix}"

    major_count = sum(
        1
        for finding in findings
        if isinstance(finding, dict)
        and str(finding.get("severity", "")) in {ReviewSeverity.MAJOR.value, ReviewSeverity.CRITICAL.value}
    )
    note_count = sum(
        1
        for finding in findings
        if isinstance(finding, dict) and str(finding.get("severity", "")) == ReviewSeverity.NOTE.value
    )
    if major_count:
        return (
            f"Review kept {len(findings)} supported finding"
            f"{'' if len(findings) == 1 else 's'}, including {major_count} blocking finding"
            f"{'' if major_count == 1 else 's'}. {suffix}"
        )
    return (
        f"Review kept {note_count} supported non-blocking finding"
        f"{'' if note_count == 1 else 's'}. {suffix}"
    )
