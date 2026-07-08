#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from pathlib import Path
import tempfile

from ..contracts import load_json_value
from github_api import (
    download_github_archive,
    extract_archive_bytes,
    github_api_json,
    github_api_query_endpoint,
)

EMPTY_REVIEW_STATE: dict[str, object] = {
    "summary": "",
    "overall_recommendation": "",
    "overall_score": 0,
    "overall_confidence": 0,
    "run_id": "",
    "head_sha": "",
    "finding_count": 0,
    "finding_count_available": False,
    "findings": [],
}


def read_json_file(path: Path) -> dict[str, object]:
    payload = load_json_value(path)
    if not isinstance(payload, dict):
        raise ValueError(f"Expected JSON object in {path}.")
    return payload


def normalize_review_state(review_state: object) -> dict[str, object]:
    if not isinstance(review_state, dict):
        return {}
    normalized = dict(review_state)
    findings = normalized.get("findings")
    if not isinstance(findings, list):
        findings = []
    normalized["findings"] = findings
    finding_count = normalized.get("finding_count")
    has_explicit_finding_count = (
        isinstance(finding_count, int)
        and not isinstance(finding_count, bool)
        and finding_count >= 0
    )
    if not has_explicit_finding_count:
        finding_count = len(findings)
    normalized["finding_count"] = finding_count
    finding_count_available = normalized.get("finding_count_available")
    if isinstance(finding_count_available, bool):
        finding_count_available = finding_count_available and has_explicit_finding_count
    else:
        finding_count_available = has_explicit_finding_count
    normalized["finding_count_available"] = finding_count_available
    return normalized


def review_state_recommendation(review_state: dict[str, object]) -> str:
    return str(review_state.get("overall_recommendation") or "").strip().lower()


def review_state_findings(review_state: dict[str, object]) -> list[object]:
    findings = review_state.get("findings")
    return findings if isinstance(findings, list) else []


def review_state_finding_count(review_state: dict[str, object]) -> int:
    finding_count = review_state.get("finding_count")
    if isinstance(finding_count, int) and not isinstance(finding_count, bool) and finding_count >= 0:
        return finding_count
    return len(review_state_findings(review_state))


def review_state_finding_count_available(review_state: dict[str, object]) -> bool:
    return review_state.get("finding_count_available") is True


def review_state_can_drive_stabilization(review_state: dict[str, object], *, source: str) -> bool:
    recommendation = review_state_recommendation(review_state)
    if not recommendation:
        return False
    if recommendation == "approve":
        return True
    findings = review_state_findings(review_state)
    return bool(source == "artifact" and findings)


def review_state_requires_findings(review_state: dict[str, object], *, source: str) -> bool:
    recommendation = review_state_recommendation(review_state)
    return bool(
        recommendation
        and recommendation != "approve"
        and not review_state_can_drive_stabilization(review_state, source=source)
    )


def missing_review_findings_error_message(
    *,
    pr_number: str,
    workflow_name: str,
    review_state: dict[str, object],
    source: str,
) -> str:
    recommendation = review_state_recommendation(review_state) or "unknown"
    run_id = str(review_state.get("run_id") or "unknown")
    finding_count = str(review_state_finding_count(review_state))
    finding_count_available = str(review_state_finding_count_available(review_state)).lower()
    recovered_findings = str(len(review_state_findings(review_state)))
    return (
        f"{workflow_name} {source} for PR #{pr_number} run {run_id} was '{recommendation}' "
        f"with finding_count={finding_count}, finding_count_available={finding_count_available}, "
        f"and recovered_findings={recovered_findings}, "
        "but complete actionable findings were not available. The stabilizer will not run from "
        "summary-only or partial Agent Review state; rerun Agent Review or wait for the "
        "agent-review-out artifact."
    )


def read_review_artifact_state(*, repository: str, run_id: str, head_sha: str) -> dict[str, object]:
    if not run_id:
        return dict()

    with tempfile.TemporaryDirectory(prefix="agent-review-artifact-") as temp_dir:
        payload = github_api_json(
            github_api_query_endpoint(
                f"repos/{repository}/actions/runs/{run_id}/artifacts",
                {"per_page": 100},
            )
        )
        artifacts = payload.get("artifacts", []) if isinstance(payload, dict) else []
        archive_url = ""
        for artifact in artifacts:
            if not isinstance(artifact, dict):
                continue
            if str(artifact.get("name") or "") != "agent-review-out":
                continue
            if bool(artifact.get("expired")):
                continue
            archive_url = str(artifact.get("archive_download_url") or "")
            if archive_url:
                break
        if not archive_url:
            return dict()

        artifact_root = Path(temp_dir) / "artifact"
        extract_archive_bytes(download_github_archive(archive_url), artifact_root)
        review_json = next(iter(sorted(artifact_root.rglob("review.json"))), None)
        if review_json is None:
            return dict()
        review_state = read_json_file(review_json)
        review_state["run_id"] = run_id
        review_state["head_sha"] = head_sha
        return normalize_review_state(review_state)


def resolve_canonical_review_state(
    *,
    repository: str,
    pr_number: str,
    workflow_name: str,
    run_id: str,
    head_sha: str,
    fallback_state: dict[str, object],
) -> tuple[dict[str, object], str]:
    if repository and run_id:
        artifact_state = normalize_review_state(
            read_review_artifact_state(
                repository=repository,
                run_id=run_id,
                head_sha=head_sha,
            )
        )
        if artifact_state:
            if review_state_can_drive_stabilization(artifact_state, source="artifact"):
                return artifact_state, "artifact"
            if review_state_requires_findings(artifact_state, source="artifact"):
                raise RuntimeError(
                    missing_review_findings_error_message(
                        pr_number=pr_number,
                        workflow_name=workflow_name,
                        review_state=artifact_state,
                        source="artifact",
                    )
                )

    return {}, ""
