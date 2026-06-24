#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import argparse
import json
import re
import os
import urllib.request
from pathlib import Path


MARKER = "<!-- codex-review-comment -->"
STATE_MARKER = "<!-- codex-review-state "
FINDING_MARKER = "<!-- codex-review-finding "

EMPTY_STATE = {
    "summary": "",
    "overall_recommendation": "comment",
    "overall_score": 0,
    "overall_confidence": 0,
    "findings": [],
}


def github_api_request(url: str, token: str) -> str:
    request = urllib.request.Request(
        url,
        headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {token}",
            "User-Agent": "amp-dev-forge-codex-review",
            "X-GitHub-Api-Version": "2022-11-28",
        },
    )
    with urllib.request.urlopen(request) as response:
        return response.read().decode("utf-8")


def list_issue_comments(repository: str, pr_number: str, token: str):
    comments = []
    page = 1
    while True:
        url = f"https://api.github.com/repos/{repository}/issues/{pr_number}/comments?per_page=100&page={page}"
        batch = json.loads(github_api_request(url, token))
        comments.extend(batch)
        if len(batch) < 100:
            break
        page += 1
    return comments


def extract_state_metadata(body: str):
    for line in body.splitlines():
        if line.startswith(STATE_MARKER) and line.endswith(" -->"):
            payload = line[len(STATE_MARKER):-4].strip()
            return json.loads(payload)
    return dict(EMPTY_STATE)


def extract_findings(body: str):
    findings = []
    current_checked = None

    for line in body.splitlines():
        checkbox_match = re.match(r"^- \[[ xX]\] ", line)
        if checkbox_match:
            current_checked = checkbox_match.group(1).lower() == "x"
            continue
        if line.startswith(FINDING_MARKER) and line.endswith(" -->"):
            payload = line[len(FINDING_MARKER):-4].strip()
            finding = json.loads(payload)
            finding["drop"] = bool(current_checked)
            findings.append(finding)
            current_checked = None

    return findings


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, help="Output JSON path.")
    args = parser.parse_args()

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    token = os.environ.get("GITHUB_TOKEN")
    repository = os.environ.get("GITHUB_REPOSITORY")
    pr_number = os.environ.get("GITHUB_PR_NUMBER")

    if not token or not repository or not pr_number:
        output_path.write_text(json.dumps(EMPTY_STATE, indent=2), encoding="utf-8")
        return

    comments = list_issue_comments(repository, pr_number, token)
    target_comment = next((comment for comment in comments if MARKER in comment.get("body", "")), None)

    if target_comment is None:
        output_path.write_text(json.dumps(EMPTY_STATE, indent=2), encoding="utf-8")
        return

    body = target_comment.get("body", "")
    state = extract_state_metadata(body)
    state["findings"] = extract_findings(body)
    output_path.write_text(json.dumps(state, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
