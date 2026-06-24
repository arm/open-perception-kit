#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import argparse
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path


MARKER = "<!-- codex-review-comment -->"
MAX_INLINE_SUGGESTION_LINES = 10
BADGE_LABEL_COLOR = "202938"
SEVERITY_COLORS = {
    "note": "1f6feb",
    "major": "d97706",
    "critical": "dc2626",
}
RECOMMENDATION_COLORS = {
    "approve": "15803d",
    "comment": "2563eb",
    "request_changes": "dc2626",
}


def format_location(finding):
    start_line = finding.get("start_line")
    end_line = finding.get("end_line")
    path = finding["path"]
    diff_side = finding.get("diff_side")
    side_suffix = f" ({diff_side})" if diff_side in {"LEFT", "RIGHT"} else ""
    if start_line is None:
        return f"{path}{side_suffix}"
    if end_line is None or end_line == start_line:
        return f"{path}:L{start_line}{side_suffix}"
    return f"{path}:L{start_line}-L{end_line}{side_suffix}"


def normalize_title(title):
    return " ".join(str(title).split())


def make_badge(label, message, color):
    label_text = urllib.parse.quote(str(label), safe="")
    message_text = urllib.parse.quote(str(message), safe="")
    return (
        f"![{label}: {message}]"
        f"(https://img.shields.io/badge/{label_text}-{message_text}-{color}"
        f"?style=flat&labelColor={BADGE_LABEL_COLOR})"
    )


def severity_badge(severity):
    color = SEVERITY_COLORS.get(severity)
    if color is None:
        return f"**{severity.upper()}**"
    return make_badge("severity", severity.upper(), color)


def severity_count_badge(severity, count):
    color = SEVERITY_COLORS.get(severity)
    if color is None:
        return f"**{severity.upper()}: {count}**"
    return make_badge(severity.upper(), count, color)


def recommendation_badge(recommendation):
    color = RECOMMENDATION_COLORS.get(recommendation)
    if color is None:
        return f"**{recommendation}**"
    return make_badge("recommendation", recommendation.replace("_", " ").upper(), color)


def score_badge(score):
    if score >= 0.85:
        color = "dc2626"
    elif score >= 0.60:
        color = "d97706"
    elif score >= 0.30:
        color = "2563eb"
    else:
        color = "15803d"
    return make_badge("score", f"{score:.2f}", color)


def confidence_badge(confidence):
    if confidence >= 0.85:
        color = "15803d"
    elif confidence >= 0.60:
        color = "2563eb"
    elif confidence >= 0.30:
        color = "d97706"
    else:
        color = "dc2626"
    return make_badge("confidence", f"{confidence:.2f}", color)


def summarize_findings(findings):
    counts = {"critical": 0, "major": 0, "note": 0}
    for finding in findings:
        severity = finding.get("severity")
        if severity in counts:
            counts[severity] += 1
    return counts


def format_markdown(review):
    findings = review.get("findings", [])
    counts = summarize_findings(findings)
    lines = [
        MARKER,
        "## Codex Review",
        "",
        recommendation_badge(review["overall_recommendation"]),
        f"{score_badge(review['overall_score'])} {confidence_badge(review['overall_confidence'])}",
        "",
        f"Findings: {severity_count_badge('critical', counts['critical'])} {severity_count_badge('major', counts['major'])} {severity_count_badge('note', counts['note'])}",
        "",
        review["summary"],
    ]

    return "\n".join(lines).rstrip() + "\n"


def build_inline_comment_body(finding):
    suggestion = finding.get("suggestion")
    use_inline_block = is_inline_suggestion_applicable(finding)
    lines = [
        f"{severity_badge(finding['severity'])} {score_badge(finding['score'])} {confidence_badge(finding['confidence'])} **{finding['title']}**",
        "",
        f"Location: `{format_location(finding)}`",
        "",
        finding["body"],
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


def list_pull_comments(repository, pr_number, token):
    comments_url = f"https://api.github.com/repos/{repository}/pulls/{pr_number}/comments"
    return list_paginated_items(comments_url, token)


def review_diff_side(finding):
    diff_side = finding.get("diff_side")
    if diff_side in {"LEFT", "RIGHT"}:
        return diff_side
    return "RIGHT"


def is_inline_suggestion_applicable(finding):
    suggestion = finding.get("suggestion")
    start_line = finding.get("start_line")
    end_line = finding.get("end_line")
    if not (
        suggestion
        and finding.get("path")
        and start_line is not None
        and review_diff_side(finding) == "RIGHT"
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
        and finding.get("diff_side") in {"LEFT", "RIGHT"}
    )


def review_event(recommendation):
    # <codex-review:suppress> This workflow intentionally maps internal
    # `comment` recommendations to GitHub approvals so non-blocking reviews land
    # as accepted-with-comments in the PR UI.
    mapping = {
        "approve": "APPROVE",
        "comment": "APPROVE",
        "request_changes": "REQUEST_CHANGES",
    }
    return mapping.get(recommendation, "COMMENT")


def create_pull_review(repository, pr_number, token, body, recommendation):
    reviews_url = f"https://api.github.com/repos/{repository}/pulls/{pr_number}/reviews"
    github_api_request(
        reviews_url,
        token,
        method="POST",
        payload={
            "body": body,
            "event": review_event(recommendation),
        },
    )


def publish_inline_comments(repository, pr_number, token, commit_id, findings):
    comments_url = f"https://api.github.com/repos/{repository}/pulls/{pr_number}/comments"
    count = 0
    for finding in findings:
        if not is_location_comment_applicable(finding):
            continue
        comment_body = build_inline_comment_body(finding)
        # <codex-review:suppress> This stateless review flow intentionally posts
        # fresh inline comments for the current run and does not reconcile or
        # delete older Codex inline comments yet.
        payload = {
            "body": comment_body,
            "commit_id": commit_id,
            "path": finding["path"],
            "line": finding["end_line"] if finding.get("end_line") is not None else finding["start_line"],
            "side": review_diff_side(finding),
        }
        if finding.get("end_line") is not None and finding["end_line"] != finding["start_line"]:
            payload["start_line"] = finding["start_line"]
            payload["start_side"] = review_diff_side(finding)
        try:
            github_api_request(comments_url, token, method="POST", payload=payload)
            count += 1
        except (urllib.error.HTTPError, urllib.error.URLError) as exc:
            print(
                "Skipping Codex inline comment publish/update for "
                f"{finding['path']}:{finding['start_line']} "
                f"({finding['title']}): {exc}",
                file=sys.stderr,
            )
            if isinstance(exc, urllib.error.HTTPError):
                body = exc.read().decode("utf-8", errors="replace")
                if body:
                    print(body, file=sys.stderr)
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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, help="Structured Codex review JSON file.")
    parser.add_argument("--markdown-out", required=True, help="Rendered markdown output path.")
    parser.add_argument(
        "--publish-pr-comment",
        action="store_true",
        help="Publish a pull request review body and inline comments using GitHub env vars.",
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
        create_pull_review(
            repository,
            pr_number,
            token,
            markdown,
            review.get("overall_recommendation", "comment"),
        )
        if head_sha:
            publish_inline_comments(
                repository,
                pr_number,
                token,
                head_sha,
                review.get("findings", []),
            )
    except urllib.error.HTTPError as exc:
        print(f"Failed to publish Codex review: {exc}", file=sys.stderr)
        body = exc.read().decode("utf-8", errors="replace")
        if body:
            print(body, file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
