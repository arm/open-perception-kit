#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

if __package__ in (None, ""):  # pragma: no cover - used for direct script execution.
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    __package__ = "agent_runtime.review"

from ..github_actions import find_latest_workflow_run_for_head, read_pr_details
from .state import EMPTY_REVIEW_STATE, read_review_artifact_state


DEFAULT_REVIEW_WORKFLOW_FILE = "agent-review.yml"


def latest_review_artifact_state(*, repository: str, pr_number: str, workflow_file: str) -> dict[str, object]:
    pr_details = read_pr_details(pr_number)
    head_sha = pr_details["head_sha"]
    repair_branch = pr_details["repair_branch"]
    if not head_sha or not repair_branch:
        return dict(EMPTY_REVIEW_STATE)
    run_id = find_latest_workflow_run_for_head(
        repository=repository,
        workflow_file=workflow_file,
        branch=repair_branch,
        head_sha=head_sha,
    )
    if not run_id:
        return dict(EMPTY_REVIEW_STATE)
    return read_review_artifact_state(
        repository=repository,
        run_id=run_id,
        head_sha=head_sha,
    ) or dict(EMPTY_REVIEW_STATE)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, help="Output JSON path.")
    parser.add_argument("--workflow-file", default=DEFAULT_REVIEW_WORKFLOW_FILE)
    args = parser.parse_args()

    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    token = os.environ.get("GITHUB_TOKEN")
    repository = os.environ.get("GITHUB_REPOSITORY")
    pr_number = os.environ.get("GITHUB_PR_NUMBER")

    if not token or not repository or not pr_number:
        output_path.write_text(json.dumps(EMPTY_REVIEW_STATE, indent=2), encoding="utf-8")
        return

    state = latest_review_artifact_state(
        repository=repository,
        pr_number=pr_number,
        workflow_file=args.workflow_file,
    )
    output_path.write_text(json.dumps(state, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
