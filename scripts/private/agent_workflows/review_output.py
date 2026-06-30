#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from pathlib import Path
from typing import Any


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

    line_count = len(file_path.read_text(encoding="utf-8", errors="replace").splitlines())
    start_line = finding.get("start_line")
    end_line = finding.get("end_line")
    if isinstance(start_line, int) and start_line > line_count:
        return True
    if isinstance(end_line, int) and end_line > line_count:
        return True
    return isinstance(start_line, int) and isinstance(end_line, int) and end_line < start_line


def _resolve_repo_path(repo_root: Path, path_value: str) -> Path:
    path = Path(path_value)
    resolved = path.resolve() if path.is_absolute() else (repo_root / path).resolve()
    root = repo_root.resolve()
    if resolved != root and root not in resolved.parents:
        raise ValueError(f"Path escapes repository root: {path_value}")
    return resolved


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
