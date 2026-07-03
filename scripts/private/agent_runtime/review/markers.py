#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import base64
import json

from ..contracts import INLINE_STATE_MARKER


def review_state_metadata(review: dict[str, object], run_id: str, head_sha: str) -> dict[str, object]:
    findings = review.get("findings", [])
    if not isinstance(findings, list):
        findings = []
    metadata: dict[str, object] = {
        "summary": review["summary"],
        "overall_recommendation": review["overall_recommendation"],
        "overall_score": review["overall_score"],
        "overall_confidence": review["overall_confidence"],
        "finding_count": len(findings),
    }
    if run_id:
        metadata["run_id"] = run_id
    if head_sha:
        metadata["head_sha"] = head_sha
    return metadata


def review_state_marker(payload: dict[str, object], marker: str) -> str:
    return f"{marker}{json.dumps(payload, separators=(',', ':'), sort_keys=True)} -->"


def inline_state_marker(payload: dict[str, object]) -> str:
    encoded_payload = base64.b64encode(
        json.dumps(payload, separators=(",", ":"), sort_keys=True).encode("utf-8")
    ).decode("ascii")
    return f"{INLINE_STATE_MARKER}{encoded_payload} -->"


def inline_state_metadata(finding: dict[str, object], run_id: str) -> dict[str, object]:
    metadata: dict[str, object] = {
        "run_id": run_id,
        "title": finding["title"],
        "severity": finding["severity"],
        "score": finding["score"],
        "confidence": finding["confidence"],
        "path": finding["path"],
        "body": finding["body"],
    }
    if "start_line" in finding:
        metadata["start_line"] = finding.get("start_line")
    if "end_line" in finding:
        metadata["end_line"] = finding.get("end_line")
    if "suggestion" in finding:
        metadata["suggestion"] = finding.get("suggestion")
    return metadata
