#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys
from typing import Any
import urllib.error

if __package__ in (None, ""):  # pragma: no cover - used for direct script execution.
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    __package__ = "agent_runtime.review"

from ..contracts import ReviewRecommendation
from .diff_anchors import build_diff_comment_anchors, review_base_ref_from_env
from .github_publish import create_pull_review
from .markdown import format_markdown
from .output_filter import filter_invalid_right_side_findings


def load_filtered_review(
    input_path: Path,
    repo_root: Path,
    *,
    diff_anchors: set[tuple[str, str, int]] | None = None,
) -> dict[str, Any]:
    review = json.loads(input_path.read_text(encoding="utf-8"))
    filtered = filter_invalid_right_side_findings(review, repo_root, diff_anchors=diff_anchors)
    if filtered != review:
        input_path.write_text(json.dumps(filtered, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return filtered


def write_github_outputs(review: dict[str, Any], output_path: Path) -> None:
    findings = review.get("findings", [])
    finding_count = len(findings) if isinstance(findings, list) else 0
    recommendation = str(
        review.get("overall_recommendation", ReviewRecommendation.COMMENT.value)
    ).strip().lower()
    with output_path.open("a", encoding="utf-8") as output_file:
        output_file.write(f"recommendation={recommendation}\n")
        output_file.write(f"finding_count={finding_count}\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, help="Structured Agent review JSON file.")
    parser.add_argument("--markdown-out", required=True, help="Rendered markdown output path.")
    parser.add_argument("--github-output", default="", help="Optional GitHub Actions output file.")
    parser.add_argument(
        "--publish-pr-comment",
        action="store_true",
        help="Publish a pull request review body and inline comments using GitHub env vars.",
    )
    args = parser.parse_args()

    diff_anchors = None
    if args.publish_pr_comment:
        diff_anchors = build_diff_comment_anchors(review_base_ref_from_env())

    review = load_filtered_review(Path(args.input), Path.cwd(), diff_anchors=diff_anchors)
    run_id = os.environ.get("GITHUB_RUN_ID", "")
    head_sha = os.environ.get("GITHUB_HEAD_SHA") or os.environ.get("GITHUB_SHA") or ""
    markdown = format_markdown(review, run_id=run_id, head_sha=head_sha)
    Path(args.markdown_out).write_text(markdown, encoding="utf-8")
    if args.github_output:
        write_github_outputs(review, Path(args.github_output))

    if not args.publish_pr_comment:
        return 0

    token = os.environ.get("GITHUB_TOKEN")
    repository = os.environ.get("GITHUB_REPOSITORY")
    pr_number = os.environ.get("GITHUB_PR_NUMBER")
    publish_head_sha = os.environ.get("GITHUB_HEAD_SHA") or ""

    if not token or not repository or not pr_number:
        print(
            "Missing GITHUB_TOKEN, GITHUB_REPOSITORY, or GITHUB_PR_NUMBER for publishing.",
            file=sys.stderr,
        )
        return 1

    try:
        findings = review.get("findings", [])
        typed_findings = []
        if isinstance(findings, list):
            typed_findings = [
                finding
                for finding in findings
                if isinstance(finding, dict)
            ]
        create_pull_review(
            repository,
            pr_number,
            token,
            markdown,
            review.get("overall_recommendation", ReviewRecommendation.COMMENT.value),
            commit_id=publish_head_sha,
            findings=typed_findings,
            run_id=run_id,
            diff_anchors=diff_anchors,
        )
    except urllib.error.HTTPError as exc:
        print(f"Failed to publish Agent review: {exc}", file=sys.stderr)
        body = exc.read().decode("utf-8", errors="replace")
        if body:
            print(body, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
