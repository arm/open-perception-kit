#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import base64
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

if __package__ in (None, ""):  # pragma: no cover - used for direct script execution.
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    __package__ = "agent_runtime"

from .contracts import (
    DiffSide,
    GITHUB_API_VERSION,
    GITHUB_REVIEW_EVENTS,
    GITHUB_USER_AGENT,
    INLINE_MARKER,
    INLINE_STATE_MARKER,
    MARKER,
    RECOMMENDATION_COLORS,
    SEVERITY_COLORS,
    STATE_MARKER,
    ReviewRecommendation,
    ReviewSeverity,
)


MAX_INLINE_SUGGESTION_LINES = 10
BADGE_LABEL_COLOR = "202938"
DIFF_HUNK_RE = re.compile(
    r"^@@ -(?P<old_start>\d+)(?:,(?P<old_count>\d+))? "
    r"\+(?P<new_start>\d+)(?:,(?P<new_count>\d+))? @@"
)


def format_location(finding):
    start_line = finding.get("start_line")
    end_line = finding.get("end_line")
    path = finding["path"]
    diff_side = finding.get("diff_side")
    side_suffix = f" ({diff_side})" if diff_side in {DiffSide.LEFT.value, DiffSide.RIGHT.value} else ""
    if start_line is None:
        return f"{path}{side_suffix}"
    if end_line is None or end_line == start_line:
        return f"{path}:L{start_line}{side_suffix}"
    return f"{path}:L{start_line}-L{end_line}{side_suffix}"


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
    counts = {severity.value: 0 for severity in ReviewSeverity}
    for finding in findings:
        severity = finding.get("severity")
        if severity in counts:
            counts[severity] += 1
    return counts


def format_finding_details(findings):
    if not findings:
        return ["No findings."]

    lines = []
    for index, finding in enumerate(findings, start=1):
        lines.extend(
            [
                f"{index}. {severity_badge(finding['severity'])} **{finding['title']}**",
                f"   Location: `{format_location(finding)}`",
                f"   Score: `{finding['score']:.2f}` Confidence: `{finding['confidence']:.2f}`",
                f"   {finding['body']}",
            ]
        )
        suggestion = finding.get("suggestion")
        if suggestion:
            lines.extend(
                [
                    "   Suggested change:",
                    "",
                    "   ```text",
                    *[f"   {line}" for line in suggestion.rstrip("\n").splitlines()],
                    "   ```",
                ]
            )
        lines.append("")
    while lines and lines[-1] == "":
        lines.pop()
    return lines


def review_state_metadata(review, run_id, head_sha):
    findings = review.get("findings", [])
    if not isinstance(findings, list):
        findings = []
    metadata = {
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


def review_state_marker(payload, marker):
    return f"{marker}{json.dumps(payload, separators=(',', ':'), sort_keys=True)} -->"


def inline_state_marker(payload):
    encoded_payload = base64.b64encode(
        json.dumps(payload, separators=(",", ":"), sort_keys=True).encode("utf-8")
    ).decode("ascii")
    return f"{INLINE_STATE_MARKER}{encoded_payload} -->"


def inline_state_metadata(finding, run_id):
    metadata = {
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


def format_markdown(review, *, run_id="", head_sha=""):
    findings = review.get("findings", [])
    counts = summarize_findings(findings)
    finding_badges = " ".join(
        [
            severity_count_badge("critical", counts["critical"]),
            severity_count_badge("major", counts["major"]),
            severity_count_badge("note", counts["note"]),
        ]
    )
    lines = [
        MARKER,
        review_state_marker(review_state_metadata(review, run_id, head_sha), STATE_MARKER),
        "## Agent Review",
        "",
        recommendation_badge(review["overall_recommendation"]),
        f"{score_badge(review['overall_score'])} {confidence_badge(review['overall_confidence'])}",
        "",
        f"Findings: {finding_badges}",
        "",
        review["summary"],
        "",
        "### Findings",
        "",
        *format_finding_details(findings),
    ]

    return "\n".join(lines).rstrip() + "\n"


def build_inline_comment_body(finding, *, run_id):
    suggestion = finding.get("suggestion")
    use_inline_block = is_inline_suggestion_applicable(finding)
    finding_badges = " ".join(
        [
            severity_badge(finding["severity"]),
            score_badge(finding["score"]),
            confidence_badge(finding["confidence"]),
        ]
    )
    lines = [
        INLINE_MARKER,
        inline_state_marker(inline_state_metadata(finding, run_id)),
        f"{finding_badges} **{finding['title']}**",
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


def review_diff_side(finding):
    diff_side = finding.get("diff_side")
    if diff_side in {DiffSide.LEFT.value, DiffSide.RIGHT.value}:
        return diff_side
    return DiffSide.RIGHT.value


def is_inline_suggestion_applicable(finding):
    suggestion = finding.get("suggestion")
    start_line = finding.get("start_line")
    end_line = finding.get("end_line")
    if not (
        suggestion
        and finding.get("path")
        and start_line is not None
        and review_diff_side(finding) == DiffSide.RIGHT.value
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
        and finding.get("diff_side") in {DiffSide.LEFT.value, DiffSide.RIGHT.value}
    )


def diff_path_from_header(value):
    if value == "/dev/null":
        return None
    if value.startswith("a/") or value.startswith("b/"):
        return value[2:]
    return value


def parse_diff_comment_anchors(diff_text):
    anchors = set()
    old_path = None
    new_path = None
    old_line = None
    new_line = None

    for line in diff_text.splitlines():
        if line.startswith("diff --git "):
            old_path = None
            new_path = None
            old_line = None
            new_line = None
            continue
        if line.startswith("--- "):
            old_path = diff_path_from_header(line[4:].split("\t", 1)[0])
            old_line = None
            new_line = None
            continue
        if line.startswith("+++ "):
            new_path = diff_path_from_header(line[4:].split("\t", 1)[0])
            old_line = None
            new_line = None
            continue
        match = DIFF_HUNK_RE.match(line)
        if match:
            old_line = int(match.group("old_start"))
            new_line = int(match.group("new_start"))
            continue
        if old_line is None or new_line is None:
            continue
        if line.startswith("\\"):
            continue
        old_anchor_path = old_path or new_path
        new_anchor_path = new_path or old_path
        if line.startswith("-"):
            if old_anchor_path is not None:
                anchors.add((old_anchor_path, DiffSide.LEFT.value, old_line))
            old_line += 1
            continue
        if line.startswith("+"):
            if new_anchor_path is not None:
                anchors.add((new_anchor_path, DiffSide.RIGHT.value, new_line))
            new_line += 1
            continue
        if line.startswith(" "):
            if new_anchor_path is not None:
                anchors.add((new_anchor_path, DiffSide.RIGHT.value, new_line))
            old_line += 1
            new_line += 1

    return anchors


def review_base_ref_from_env():
    review_base_ref = os.environ.get("REVIEW_BASE_REF")
    if review_base_ref:
        return review_base_ref
    github_base_ref = os.environ.get("GITHUB_BASE_REF")
    if github_base_ref:
        return f"origin/{github_base_ref}"
    return ""


def build_diff_comment_anchors(base_ref, head_ref="HEAD"):
    if not base_ref:
        return None
    try:
        completed = subprocess.run(
            ["git", "diff", "--no-ext-diff", f"{base_ref}...{head_ref}"],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        print(
            f"Failed to build Agent review diff anchors from {base_ref}...{head_ref}: {exc}",
            file=sys.stderr,
        )
        return None
    return parse_diff_comment_anchors(completed.stdout)


def is_review_comment_payload_anchored(comment, diff_anchors):
    path = comment["path"]
    side = comment["side"]
    line = comment["line"]
    if "start_line" not in comment:
        return (path, side, line) in diff_anchors
    start_line = comment["start_line"]
    start_side = comment["start_side"]
    if start_side != side or start_line > line:
        return False
    return all(
        (path, side, candidate) in diff_anchors
        for candidate in range(start_line, line + 1)
    )


def filter_review_comment_payloads_for_diff(comments, diff_anchors):
    if diff_anchors is None:
        return comments
    return [
        comment
        for comment in comments
        if is_review_comment_payload_anchored(comment, diff_anchors)
    ]


def review_event(recommendation):
    return GITHUB_REVIEW_EVENTS.get(recommendation, "COMMENT")


def is_review_comment_validation_error(exc):
    return getattr(exc, "code", None) == 422


def build_review_comment_payload(finding, *, run_id):
    side = review_diff_side(finding)
    payload = {
        "body": build_inline_comment_body(finding, run_id=run_id),
        "path": finding["path"],
        "line": finding["end_line"]
        if finding.get("end_line") is not None
        else finding["start_line"],
        "side": side,
    }
    # A single invalid anchor rejects the whole batched review. Keep deleted
    # LEFT-side findings on one stable deleted line instead of risking a range.
    if (
        side == DiffSide.RIGHT.value
        and finding.get("end_line") is not None
        and finding["end_line"] != finding["start_line"]
    ):
        payload["start_line"] = finding["start_line"]
        payload["start_side"] = side
    return payload


def build_review_comment_payloads(findings, *, run_id, diff_anchors=None):
    comments = []
    for finding in findings:
        if not is_location_comment_applicable(finding):
            continue
        comments.append(build_review_comment_payload(finding, run_id=run_id))
    return filter_review_comment_payloads_for_diff(comments, diff_anchors)


def submit_pull_review(repository, pr_number, token, payload):
    reviews_url = f"https://api.github.com/repos/{repository}/pulls/{pr_number}/reviews"
    github_api_request(
        reviews_url,
        token,
        method="POST",
        payload=payload,
    )


def create_pull_review(
    repository,
    pr_number,
    token,
    body,
    recommendation,
    *,
    commit_id="",
    findings=None,
    run_id="",
    diff_anchors=None,
):
    payload = {
        "body": body,
        "event": review_event(recommendation),
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


def github_api_request(url, token, method="GET", payload=None):
    data = None
    headers = {
        "Accept": "application/vnd.github+json",
        "Authorization": f"Bearer {token}",
        "User-Agent": GITHUB_USER_AGENT,
        "X-GitHub-Api-Version": GITHUB_API_VERSION,
    }
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    request = urllib.request.Request(url, data=data, headers=headers, method=method)
    with urllib.request.urlopen(request) as response:
        return response.read().decode("utf-8")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, help="Structured Agent review JSON file.")
    parser.add_argument("--markdown-out", required=True, help="Rendered markdown output path.")
    parser.add_argument(
        "--publish-pr-comment",
        action="store_true",
        help="Publish a pull request review body and inline comments using GitHub env vars.",
    )
    args = parser.parse_args()

    review = json.loads(Path(args.input).read_text(encoding="utf-8"))
    run_id = os.environ.get("GITHUB_RUN_ID", "")
    head_sha = os.environ.get("GITHUB_HEAD_SHA") or os.environ.get("GITHUB_SHA", "")
    markdown = format_markdown(review, run_id=run_id, head_sha=head_sha)
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

    diff_anchors = build_diff_comment_anchors(review_base_ref_from_env())

    try:
        create_pull_review(
            repository,
            pr_number,
            token,
            markdown,
            review.get("overall_recommendation", ReviewRecommendation.COMMENT.value),
            commit_id=head_sha or "",
            findings=review.get("findings", []),
            run_id=run_id,
            diff_anchors=diff_anchors,
        )
    except urllib.error.HTTPError as exc:
        print(f"Failed to publish Agent review: {exc}", file=sys.stderr)
        body = exc.read().decode("utf-8", errors="replace")
        if body:
            print(body, file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
