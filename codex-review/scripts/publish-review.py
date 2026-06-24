#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import argparse
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path


MARKER = "<!-- codex-review-comment -->"
INLINE_MARKER = "<!-- codex-review-inline -->"
STATE_MARKER = "<!-- codex-review-state "
FINDING_MARKER = "<!-- codex-review-finding "
MAX_INLINE_SUGGESTION_LINES = 4


def format_location(finding):
    start_line = finding.get("start_line")
    end_line = finding.get("end_line")
    path = finding["path"]
    if start_line is None:
        return path
    if end_line is None or end_line == start_line:
        return f"{path}:L{start_line}"
    return f"{path}:L{start_line}-L{end_line}"


def format_markdown(review):
    findings = review.get("findings", [])
    state_payload = json.dumps(
        {
            "summary": review["summary"],
            "overall_recommendation": review["overall_recommendation"],
            "overall_score": review["overall_score"],
            "overall_confidence": review["overall_confidence"],
        },
        separators=(",", ":"),
        sort_keys=True,
    )
    lines = [
        MARKER,
        "## Codex Review",
        "",
        f"Recommendation: **{review['overall_recommendation']}**",
        f"Overall score: **{review['overall_score']:.2f}**",
        f"Overall confidence: **{review['overall_confidence']:.2f}**",
        "",
        review["summary"],
        "",
        f"{STATE_MARKER}{state_payload} -->",
        "",
    ]

    if not findings:
        lines.extend(
            [
                "### Findings",
                "",
                "No concrete findings were reported.",
            ]
        )
        return "\n".join(lines) + "\n"

    lines.extend(["### Findings", ""])
    for finding in findings:
        metadata_payload = json.dumps(
            {
                "title": finding["title"],
                "severity": finding["severity"],
                "score": finding["score"],
                "confidence": finding["confidence"],
                "path": finding["path"],
                "start_line": finding.get("start_line"),
                "end_line": finding.get("end_line"),
                "body": finding["body"],
                "suggestion": finding.get("suggestion"),
            },
            separators=(",", ":"),
            sort_keys=True,
        )
        lines.extend(
            [
                f"- [ ] **[{finding['severity']}] {finding['title']}**",
                f"  Location: `{format_location(finding)}`",
                f"  Score: `{finding['score']:.2f}`",
                f"  Confidence: `{finding['confidence']:.2f}`",
                f"  {finding['body']}",
            ]
        )
        suggestion = finding.get("suggestion")
        if suggestion:
            lines.append(f"  Suggested direction: {suggestion}")
        lines.extend(
            [
                f"{FINDING_MARKER}{metadata_payload} -->",
                "",
            ]
        )

    return "\n".join(lines).rstrip() + "\n"


def build_inline_comment_body(finding):
    suggestion = finding.get("suggestion")
    use_inline_block = is_inline_suggestion_applicable(finding)
    lines = [
        INLINE_MARKER,
        f"**[{finding['severity']}] {finding['title']}**",
        "",
        finding["body"],
        "",
        f"Score: `{finding['score']:.2f}`",
        f"Confidence: `{finding['confidence']:.2f}`",
    ]

    if suggestion:
        if use_inline_block:
            lines.extend(
                [
                    "",
                    "```suggestion",
                    suggestion.rstrip("\n"),
                    "```",
                ]
            )
        else:
            lines.extend(
                [
                    "",
                    "Suggested change:",
                    "",
                    "```text",
                    suggestion.rstrip("\n"),
                    "```",
                ]
            )

    return "\n".join(lines) + "\n"


def list_paginated_items(url, token):
    items = []
    page = 1
    separator = "&" if "?" in url else "?"

    while True:
        page_url = f"{url}{separator}per_page=100&page={page}"
        page_items = json.loads(github_api_request(page_url, token))
        if not page_items:
            break
        items.extend(page_items)
        if len(page_items) < 100:
            break
        page += 1

    return items


def is_inline_suggestion_applicable(finding):
    suggestion = finding.get("suggestion")
    start_line = finding.get("start_line")
    end_line = finding.get("end_line")
    if not (
        suggestion
        and finding.get("path")
        and start_line is not None
    ):
        return False

    effective_end_line = end_line if end_line is not None else start_line
    touched_lines = (effective_end_line - start_line) + 1
    suggestion_lines = len(suggestion.rstrip("\n").splitlines())

    return bool(
        touched_lines <= MAX_INLINE_SUGGESTION_LINES
        and suggestion_lines <= MAX_INLINE_SUGGESTION_LINES
    )


def is_location_comment_applicable(finding):
    return bool(
        finding.get("path")
        and finding.get("start_line") is not None
    )


def publish_inline_comments(repository, pr_number, token, commit_id, findings):
    comments_url = f"https://api.github.com/repos/{repository}/pulls/{pr_number}/comments"
    count = 0
    for finding in findings:
        if not is_location_comment_applicable(finding):
            continue
        payload = {
            "body": build_inline_comment_body(finding),
            "commit_id": commit_id,
            "path": finding["path"],
            "line": finding["end_line"] if finding.get("end_line") is not None else finding["start_line"],
            "side": "RIGHT",
        }
        if finding.get("end_line") is not None and finding["end_line"] != finding["start_line"]:
            payload["start_line"] = finding["start_line"]
            payload["start_side"] = "RIGHT"
        github_api_request(comments_url, token, method="POST", payload=payload)
        count += 1
    return count


def github_api_request(url, token, method="GET", payload=None):
    data = None
    headers = {
        "Accept": "application/vnd.github+json",
        "Authorization": f"Bearer {token}",
        "User-Agent": "amp-dev-forge-codex-review",
        "X-GitHub-Api-Version": "2022-11-28",
    }
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    request = urllib.request.Request(url, data=data, headers=headers, method=method)
    with urllib.request.urlopen(request) as response:
        return response.read().decode("utf-8")


def upsert_issue_comment(repository, pr_number, token, body):
    comments_url = f"https://api.github.com/repos/{repository}/issues/{pr_number}/comments"
    comments = list_paginated_items(comments_url, token)
    for comment in comments:
        if MARKER in comment.get("body", ""):
            edit_url = f"https://api.github.com/repos/{repository}/issues/comments/{comment['id']}"
            github_api_request(edit_url, token, method="PATCH", payload={"body": body})
            return
    github_api_request(comments_url, token, method="POST", payload={"body": body})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, help="Structured Codex review JSON file.")
    parser.add_argument("--markdown-out", required=True, help="Rendered markdown output path.")
    parser.add_argument(
        "--publish-pr-comment",
        action="store_true",
        help="Publish or update a pull request comment using GITHUB_TOKEN and PR env vars.",
    )
    args = parser.parse_args()

    review = json.loads(Path(args.input).read_text(encoding="utf-8"))
    markdown = format_markdown(review)
    Path(args.markdown_out).write_text(markdown, encoding="utf-8")

    if not args.publish_pr_comment:
        return

    token = os.environ.get("GITHUB_TOKEN")
    repository = os.environ.get("GITHUB_REPOSITORY")
    pr_number = os.environ.get("GITHUB_PR_NUMBER")
    head_sha = os.environ.get("GITHUB_HEAD_SHA")

    if not token or not repository or not pr_number:
        print(
            "Missing GITHUB_TOKEN, GITHUB_REPOSITORY, or GITHUB_PR_NUMBER for publishing.",
            file=sys.stderr,
        )
        sys.exit(1)

    try:
        upsert_issue_comment(repository, pr_number, token, markdown)
        if head_sha:
            publish_inline_comments(
                repository,
                pr_number,
                token,
                head_sha,
                review.get("findings", []),
            )
    except urllib.error.HTTPError as exc:
        print(f"Failed to publish Codex review comment: {exc}", file=sys.stderr)
        body = exc.read().decode("utf-8", errors="replace")
        if body:
            print(body, file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
