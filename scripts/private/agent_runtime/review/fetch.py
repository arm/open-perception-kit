#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import base64
import json
import os
import sys
import urllib.error
from pathlib import Path

if __package__ in (None, ""):  # pragma: no cover - used for direct script execution.
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    __package__ = "agent_runtime.review"

from ..contracts import (
    DEFAULT_AUTHOR_LOGINS,
    INLINE_MARKER,
    INLINE_STATE_MARKER,
    MARKER,
    STATE_MARKER,
)
from ..github_api import (
    github_api_endpoint_url,
    github_api_query_endpoint,
    list_paginated_items,
)
from .state import EMPTY_REVIEW_STATE, normalize_review_summary_state
REQUIRED_INLINE_METADATA_FIELDS = (
    "title",
    "severity",
    "score",
    "confidence",
    "path",
    "body",
)


def list_issue_comments(repository: str, pr_number: str, token: str):
    return list_paginated_items(
        github_api_endpoint_url(
            github_api_query_endpoint(
                f"repos/{repository}/issues/{pr_number}/comments",
                {"per_page": 100},
            )
        ),
        token=token,
    )


def list_pull_comments(repository: str, pr_number: str, token: str):
    return list_paginated_items(
        github_api_endpoint_url(
            github_api_query_endpoint(
                f"repos/{repository}/pulls/{pr_number}/comments",
                {"per_page": 100},
            )
        ),
        token=token,
    )


def list_pull_reviews(repository: str, pr_number: str, token: str):
    return list_paginated_items(
        github_api_endpoint_url(
            github_api_query_endpoint(
                f"repos/{repository}/pulls/{pr_number}/reviews",
                {"per_page": 100},
            )
        ),
        token=token,
    )


def allowed_author_logins():
    configured = os.environ.get("AGENT_REVIEW_AUTHOR_LOGINS", "")
    logins = {entry.strip() for entry in configured.split(",") if entry.strip()}
    return logins | set(DEFAULT_AUTHOR_LOGINS) if logins else set(DEFAULT_AUTHOR_LOGINS)


def comment_author_login(comment) -> str:
    return str(dict(comment.get("user") or {}).get("login") or "")


def extract_state_metadata(body: str):
    for line in body.splitlines():
        if line.startswith(STATE_MARKER) and line.endswith(" -->"):
            payload = line[len(STATE_MARKER):-4].strip()
            try:
                return normalize_review_summary_state(json.loads(payload))
            except json.JSONDecodeError:
                print(
                    "Ignoring malformed Agent review state marker JSON.",
                    file=sys.stderr,
                )
    return dict(EMPTY_REVIEW_STATE)


def extract_inline_metadata(body: str):
    for line in body.splitlines():
        if line.startswith(INLINE_STATE_MARKER) and line.endswith(" -->"):
            payload = line[len(INLINE_STATE_MARKER):-4].strip()
            try:
                decoded_payload = base64.b64decode(payload, validate=True).decode("utf-8")
                return json.loads(decoded_payload)
            except (ValueError, UnicodeDecodeError, json.JSONDecodeError):
                print(
                    "Ignoring malformed Agent inline state marker JSON.",
                    file=sys.stderr,
                )
    return None


def comment_timestamp(comment) -> str:
    for field in ("submitted_at", "created_at", "updated_at"):
        value = str(comment.get(field) or "")
        if value:
            return value
    return ""


def summary_state_comments(issue_comments, pull_reviews, author_logins):
    comments = [
        comment
        for comment in [*issue_comments, *pull_reviews]
        if MARKER in comment.get("body", "") and comment_author_login(comment) in author_logins
    ]
    return sorted(comments, key=comment_timestamp)


def extract_findings(comments, run_id: str, author_logins):
    findings_by_key = {}

    for comment in comments:
        body = comment.get("body", "")
        if INLINE_MARKER not in body:
            continue
        if comment_author_login(comment) not in author_logins:
            continue
        metadata = extract_inline_metadata(body)
        if metadata is None:
            continue
        if metadata.get("run_id") != run_id:
            continue
        missing_fields = [
            field for field in REQUIRED_INLINE_METADATA_FIELDS if field not in metadata
        ]
        if missing_fields:
            print(
                "Ignoring Agent inline comment with incomplete state metadata: "
                + ", ".join(missing_fields),
                file=sys.stderr,
            )
            continue

        finding = {
            "title": metadata["title"],
            "severity": metadata["severity"],
            "score": metadata["score"],
            "confidence": metadata["confidence"],
            "path": metadata["path"],
            "diff_side": metadata.get("diff_side"),
            "start_line": metadata.get("start_line"),
            "end_line": metadata.get("end_line"),
            "body": metadata["body"],
            "suggestion": metadata.get("suggestion"),
        }
        key = json.dumps(
            {
                "path": finding["path"],
                "start_line": finding["start_line"],
                "end_line": finding["end_line"],
                "title": finding["title"],
            },
            separators=(",", ":"),
            sort_keys=True,
        )
        findings_by_key[key] = finding

    return list(findings_by_key.values())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, help="Output JSON path.")
    args = parser.parse_args()

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    token = os.environ.get("GITHUB_TOKEN")
    repository = os.environ.get("GITHUB_REPOSITORY")
    pr_number = os.environ.get("GITHUB_PR_NUMBER")
    author_logins = allowed_author_logins()

    if not token or not repository or not pr_number:
        output_path.write_text(json.dumps(EMPTY_REVIEW_STATE, indent=2), encoding="utf-8")
        return

    issue_comments = []
    pull_reviews = []
    try:
        issue_comments = list_issue_comments(repository, pr_number, token)
    except (urllib.error.HTTPError, urllib.error.URLError) as exc:
        print(
            f"Failed to fetch existing Agent issue comments; proceeding without them: {exc}",
            file=sys.stderr,
        )
    try:
        pull_reviews = list_pull_reviews(repository, pr_number, token)
    except (urllib.error.HTTPError, urllib.error.URLError) as exc:
        print(
            f"Failed to fetch existing Agent pull reviews; proceeding without them: {exc}",
            file=sys.stderr,
        )

    summary_comments = summary_state_comments(issue_comments, pull_reviews, author_logins)
    if not summary_comments:
        output_path.write_text(json.dumps(EMPTY_REVIEW_STATE, indent=2), encoding="utf-8")
        return

    target_comment = summary_comments[-1]
    state = extract_state_metadata(target_comment.get("body", ""))
    run_id = state.get("run_id")
    if not run_id:
        output_path.write_text(json.dumps(EMPTY_REVIEW_STATE, indent=2), encoding="utf-8")
        return

    try:
        pull_comments = list_pull_comments(repository, pr_number, token)
    except (urllib.error.HTTPError, urllib.error.URLError) as exc:
        print(
            f"Failed to fetch existing Agent inline comments; proceeding without prior findings: {exc}",
            file=sys.stderr,
        )
        output_path.write_text(json.dumps(state, indent=2), encoding="utf-8")
        return

    state["findings"] = extract_findings(pull_comments, run_id, author_logins)
    if state.get("finding_count_available") is not True:
        state["finding_count"] = len(state["findings"])
        state["finding_count_available"] = True
    output_path.write_text(json.dumps(state, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
