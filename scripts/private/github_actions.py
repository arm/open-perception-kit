#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from __future__ import annotations

import os
import tempfile
from datetime import datetime
from pathlib import Path
import urllib.error
import zipfile

from github_api import (
    download_github_archive,
    extract_archive_bytes,
    github_api_base_url,
    github_api_json,
    github_api_query_endpoint,
)


def parse_timestamp(value: str) -> datetime:
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def required_pr_value(value: object, name: str, pr_number: str) -> str:
    result = str(value or "").strip()
    if not result:
        raise RuntimeError(f"Unable to resolve {name} for PR #{pr_number}.")
    return result


def require_same_repository(
    actual: str, expected: str, role: str, pr_number: str
) -> None:
    if actual != expected:
        raise RuntimeError(
            "Agent workflow automation only supports same-repository pull requests; "
            f"PR #{pr_number} {role} repository is '{actual}', expected '{expected}'."
        )


def read_pr_details(pr_number: str, *, repository: str | None = None) -> dict[str, str]:
    repository = (repository or os.environ.get("GITHUB_REPOSITORY") or "").strip()
    if not repository:
        raise RuntimeError("GITHUB_REPOSITORY is required to resolve PR details.")
    payload = github_api_json(f"repos/{repository}/pulls/{pr_number}")
    if not isinstance(payload, dict):
        raise RuntimeError(f"Unexpected PR payload for PR #{pr_number}.")
    head = dict(payload.get("head") or {})
    base = dict(payload.get("base") or {})
    head_repository = required_pr_value(
        dict(head.get("repo") or {}).get("full_name"), "head repository", pr_number
    )
    base_repository = required_pr_value(
        dict(base.get("repo") or {}).get("full_name"), "base repository", pr_number
    )
    target_branch = required_pr_value(base.get("ref"), "target branch", pr_number)
    require_same_repository(head_repository, repository, "head", pr_number)
    require_same_repository(base_repository, repository, "base", pr_number)
    return {
        "title": str(payload.get("title") or ""),
        "base_sha": str(base.get("sha") or ""),
        "head_branch": str(head.get("ref") or ""),
        "head_sha": str(head.get("sha") or ""),
        "target_branch": target_branch,
        "head_repository": head_repository,
        "base_repository": base_repository,
    }


def read_workflow_run(*, repository: str, run_id: str) -> object:
    return github_api_json(f"repos/{repository}/actions/runs/{run_id}")


def source_pull_request_numbers(run_json: dict[str, object]) -> list[str]:
    pull_requests = run_json.get("pull_requests", [])
    if not isinstance(pull_requests, list):
        return []

    numbers: list[str] = []
    for pull_request in pull_requests:
        if not isinstance(pull_request, dict):
            continue
        number = str(pull_request.get("number") or "").strip()
        if not number:
            url = str(pull_request.get("url") or "").strip()
            number = url.rstrip("/").rsplit("/", 1)[-1] if "/pulls/" in url else ""
        if number and number not in numbers:
            numbers.append(number)
    return numbers


def issue_label_names(issue_payload: object) -> set[str]:
    if not isinstance(issue_payload, dict):
        return set()
    labels = issue_payload.get("labels", [])
    if not isinstance(labels, list):
        return set()

    names: set[str] = set()
    for label in labels:
        if not isinstance(label, dict):
            continue
        name = str(label.get("name") or "").strip()
        if name:
            names.add(name)
    return names


def authorized_source_pr_number(
    *,
    repository: str,
    source_pr_numbers: list[str],
    authorization_label: str,
) -> str:
    for pr_number in source_pr_numbers:
        issue_payload = github_api_json(f"repos/{repository}/issues/{pr_number}")
        if authorization_label in issue_label_names(issue_payload):
            return pr_number
    return ""


def write_workflow_run_log_file(
    *,
    repository: str,
    run_id: str,
    output_path: Path,
    fallback_url: str,
) -> None:
    try:
        log_archive = download_github_archive(
            f"{github_api_base_url()}/repos/{repository}/actions/runs/{run_id}/logs",
        )
        with tempfile.TemporaryDirectory(prefix="agent-repair-source-run-logs-") as temp_dir:
            log_root = Path(temp_dir) / "logs"
            log_files = extract_archive_bytes(log_archive, log_root)
            if not log_files:
                raise RuntimeError("Run log archive did not contain any files.")
            with output_path.open("w", encoding="utf-8") as output_file:
                for log_file in log_files:
                    relative_name = log_file.relative_to(log_root).as_posix()
                    log_text = log_file.read_text(encoding="utf-8", errors="replace")
                    output_file.write(f"===== {relative_name} =====\n")
                    output_file.write(log_text)
                    if not log_text.endswith("\n"):
                        output_file.write("\n")
                    output_file.write("\n")
    except (OSError, RuntimeError, urllib.error.HTTPError, urllib.error.URLError, zipfile.BadZipFile):
        output_path.write_text(f"Run logs were unavailable for {fallback_url}\n", encoding="utf-8")


def download_workflow_run_artifacts(*, repository: str, run_id: str, artifact_root: Path) -> None:
    try:
        payload = github_api_json(
            github_api_query_endpoint(
                f"repos/{repository}/actions/runs/{run_id}/artifacts",
                {"per_page": 100},
            )
        )
        artifacts = payload.get("artifacts", []) if isinstance(payload, dict) else []
        for artifact in artifacts:
            if not isinstance(artifact, dict) or bool(artifact.get("expired")):
                continue
            archive_url = str(artifact.get("archive_download_url") or "")
            artifact_name = str(artifact.get("name") or "").strip()
            artifact_id = str(artifact.get("id") or "").strip()
            if not archive_url or not artifact_name:
                continue
            destination = artifact_root / artifact_name
            if destination.exists():
                destination = artifact_root / f"{artifact_name}-{artifact_id or 'artifact'}"
            extract_archive_bytes(download_github_archive(archive_url), destination)
    except (OSError, RuntimeError, urllib.error.HTTPError, urllib.error.URLError, zipfile.BadZipFile):
        return


def find_latest_workflow_run_candidate(
    *,
    repository: str,
    workflow_file: str,
    branch: str,
    head_sha: str,
    events: list[str] | None = None,
    created_after: datetime | None = None,
) -> dict[str, str]:
    payload = github_api_json(
        github_api_query_endpoint(
            f"repos/{repository}/actions/workflows/{workflow_file}/runs",
            {"branch": branch, "per_page": 20},
        ),
    )
    workflow_runs = payload.get("workflow_runs", []) if isinstance(payload, dict) else []
    candidates: list[tuple[datetime, dict[str, str]]] = []
    allowed_events = set(events or [])
    for run in workflow_runs:
        if not isinstance(run, dict):
            continue
        created_at = str(run.get("created_at") or "")
        event = str(run.get("event") or "")
        if not created_at or str(run.get("head_sha") or "") != head_sha:
            continue
        if allowed_events and event not in allowed_events:
            continue
        if created_after is not None and parse_timestamp(created_at) < created_after:
            continue
        candidates.append(
            (
                parse_timestamp(created_at),
                {
                    "id": str(run.get("id") or ""),
                    "event": event,
                    "status": str(run.get("status") or ""),
                    "conclusion": str(run.get("conclusion") or ""),
                    "created_at": created_at,
                },
            )
        )
    if not candidates:
        return {}
    return max(candidates, key=lambda item: item[0])[1]


def find_latest_workflow_run_for_head(
    *,
    repository: str,
    workflow_file: str,
    branch: str,
    head_sha: str,
) -> str:
    candidate = find_latest_workflow_run_candidate(
        repository=repository,
        workflow_file=workflow_file,
        branch=branch,
        head_sha=head_sha,
    )
    return str(candidate.get("id") or "")
