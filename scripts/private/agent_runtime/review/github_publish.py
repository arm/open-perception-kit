#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import sys
import urllib.error

from github_api import github_api_request
from .comments import build_review_comment_payloads


def is_review_comment_validation_error(exc: urllib.error.HTTPError) -> bool:
    return getattr(exc, "code", None) == 422


def submit_pull_review(
    repository: str,
    pr_number: str,
    token: str,
    payload: dict[str, object],
) -> None:
    github_api_request(
        f"repos/{repository}/pulls/{pr_number}/reviews",
        token=token,
        method="POST",
        payload=payload,
    )


def create_pull_review(
    repository: str,
    pr_number: str,
    token: str,
    body: str,
    recommendation: str,
    *,
    commit_id: str = "",
    findings: list[dict[str, object]] | None = None,
    run_id: str = "",
    diff_anchors: set[tuple[str, str, int]] | None = None,
) -> None:
    payload: dict[str, object] = {
        "body": body,
        "event": "COMMENT",
    }
    if commit_id:
        payload["commit_id"] = commit_id
    comments = build_review_comment_payloads(
        findings or [],
        run_id=run_id,
        diff_anchors=diff_anchors,
    )
    if comments:
        payload["comments"] = comments
    try:
        submit_pull_review(repository, pr_number, token, payload)
    except urllib.error.HTTPError as exc:
        if (
            not comments
            or diff_anchors is not None
            or not is_review_comment_validation_error(exc)
        ):
            raise
        print(
            "Failed to attach Agent inline comments to the pull request review; "
            "retrying with the review body only.",
            file=sys.stderr,
        )
        details = exc.read().decode("utf-8", errors="replace")
        if details:
            print(details, file=sys.stderr)
        fallback_payload = {
            key: value for key, value in payload.items() if key != "comments"
        }
        submit_pull_review(repository, pr_number, token, fallback_payload)
