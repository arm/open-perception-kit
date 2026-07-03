#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess


PR_FIELDS = "baseRefName,headRefName,headRefOid"


def write_outputs(values: dict[str, str], output_path: str | None = None) -> None:
    target = output_path or os.environ.get("GITHUB_OUTPUT")
    if not target:
        raise ValueError("GITHUB_OUTPUT is not set and no explicit output path was provided.")
    with Path(target).open("a", encoding="utf-8") as output_file:
        for key, value in values.items():
            output_file.write(f"{key}={value}\n")


def resolve_pr_context(*, pr_number: str, repo: str) -> dict[str, str]:
    completed = subprocess.run(
        [
            "gh",
            "pr",
            "view",
            pr_number,
            "--repo",
            repo,
            "--json",
            PR_FIELDS,
        ],
        check=True,
        text=True,
        capture_output=True,
    )
    payload = json.loads(completed.stdout)
    if not isinstance(payload, dict):
        raise RuntimeError(f"Unexpected PR context payload for PR #{pr_number}.")
    return {
        "base_ref": str(payload.get("baseRefName") or ""),
        "head_ref": str(payload.get("headRefName") or ""),
        "head_sha": str(payload.get("headRefOid") or ""),
    }


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Resolve GitHub pull request refs for CI workflow_dispatch runs.")
    parser.add_argument("--pr-number", required=True)
    parser.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", ""))
    parser.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT", ""))
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if not args.repo:
        raise RuntimeError("--repo or GITHUB_REPOSITORY is required.")
    write_outputs(
        resolve_pr_context(pr_number=args.pr_number, repo=args.repo),
        args.github_output,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
