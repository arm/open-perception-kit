#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from pathlib import Path
from typing import Any


REMOVED_REVIEW_CONTRACT_TOKENS = (
    "codex-review",
    "codex-stabilize-pr.yml",
    "codex_model",
    "CODEX_REVIEW",
    "openai/codex-action@v1",
    "codex exec",
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
    suffix = (
        f" Omitted {dropped_count} unsupported finding"
        f"{'' if dropped_count == 1 else 's'} whose RIGHT-side file anchors do not exist"
        " in the current checkout."
    )
    filtered["summary"] = str(filtered.get("summary", "")).rstrip() + suffix
    return filtered


def _has_invalid_right_side_anchor(finding: dict[str, Any], repo_root: Path) -> bool:
    if finding.get("diff_side") != "RIGHT":
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
        token in finding_text and token not in anchor_text
        for token in REMOVED_REVIEW_CONTRACT_TOKENS
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
    if severities.intersection({"major", "critical"}):
        return "request_changes"
    if findings:
        return "comment"
    return "approve"
