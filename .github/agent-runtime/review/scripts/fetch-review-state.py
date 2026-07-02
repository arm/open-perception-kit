#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import argparse
import base64
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path

from review_contract import (
    DEFAULT_AUTHOR_LOGINS,
    GITHUB_API_VERSION,
    GITHUB_USER_AGENT,
    INLINE_MARKER,
    INLINE_STATE_MARKER,
    MARKER,
    STATE_MARKER,
)

EMPTY_STATE = {
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
REQUIRED_INLINE_METADATA_FIELDS = (
    "title",
    "severity",
    "score",
    "confidence",
    "path",
    "body",
)


def github_api_request(url: str, token: str) -> str:
    request = urllib.request.Request(
        url,
        headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {token}",
            "User-Agent": GITHUB_USER_AGENT,
            "X-GitHub-Api-Version": GITHUB_API_VERSION,
        },
    )
    with urllib.request.urlopen(request) as response:
        return response.read().decode("utf-8")


def list_paginated_items(url: str, token: str):
    items = []
    page = 1
    separator = "&" if "?" in url else "?"
    while True:
        page_url = f"{url}{separator}per_page=100&page={page}"
        batch = json.loads(github_api_request(page_url, token))
        if not batch:
            break
        items.extend(batch)
        if len(batch) < 100:
            break
        page += 1
    return items


def list_issue_comments(repository: str, pr_number: str, token: str):
    return list_paginated_items(
        f"https://api.github.com/repos/{repository}/issues/{pr_number}/comments",
        token,
    )


def list_pull_comments(repository: str, pr_number: str, token: str):
    return list_paginated_items(
        f"https://api.github.com/repos/{repository}/pulls/{pr_number}/comments",
        token,
    )


def list_pull_reviews(repository: str, pr_number: str, token: str):
    return list_paginated_items(
        f"https://api.github.com/repos/{repository}/pulls/{pr_number}/reviews",
        token,
    )


def allowed_author_logins():
    configured = os.environ.get("AGENT_REVIEW_AUTHOR_LOGINS", "")
    logins = {entry.strip() for entry in configured.split(",") if entry.strip()}
    return logins | set(DEFAULT_AUTHOR_LOGINS) if logins else set(DEFAULT_AUTHOR_LOGINS)


def comment_author_login(comment) -> str:
    return str(dict(comment.get("user") or {}).get("login") or "")


def normalize_state_metadata(state):
    if not isinstance(state, dict):
        return dict(EMPTY_STATE)

    normalized = dict(EMPTY_STATE)
    normalized.update(state)

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
    normalized["finding_count_available"] = has_explicit_finding_count
    return normalized


def extract_state_metadata(body: str):
    for line in body.splitlines():
        if line.startswith(STATE_MARKER) and line.endswith(" -->"):
            payload = line[len(STATE_MARKER):-4].strip()
            try:
                return normalize_state_metadata(json.loads(payload))
            except json.JSONDecodeError:
                print(
                    "Ignoring malformed Agent review state marker JSON.",
                    file=sys.stderr,
                )
    return dict(EMPTY_STATE)


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
        output_path.write_text(json.dumps(EMPTY_STATE, indent=2), encoding="utf-8")
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
        output_path.write_text(json.dumps(EMPTY_STATE, indent=2), encoding="utf-8")
        return

    target_comment = summary_comments[-1]
    state = extract_state_metadata(target_comment.get("body", ""))
    run_id = state.get("run_id")
    if not run_id:
        output_path.write_text(json.dumps(EMPTY_STATE, indent=2), encoding="utf-8")
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
