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


PR_FIELDS = "baseRefName,headRefName,headRefOid,isCrossRepository"


def write_outputs(values: dict[str, str], output_path: str | None = None) -> None:
    target = output_path or os.environ.get("GITHUB_OUTPUT")
    if not target:
        raise ValueError("GITHUB_OUTPUT is not set and no explicit output path was provided.")
    with Path(target).open("a", encoding="utf-8") as output_file:
        for key, value in values.items():
            output_file.write(f"{key}={value}\n")


def _required_ref(payload: dict[object, object], field: str, pr_number: str) -> str:
    value = payload.get(field)
    if not isinstance(value, str) or not value:
        raise RuntimeError(f"Incomplete pull request refs for PR #{pr_number}.")
    return value


def _apply_manual_overrides(
    context: dict[str, str],
    *,
    base_ref_override: str = "",
    head_ref_override: str = "",
    head_sha_override: str = "",
) -> dict[str, str]:
    resolved = dict(context)
    base_ref_override = str(base_ref_override or "")
    head_ref_override = str(head_ref_override or "")
    head_sha_override = str(head_sha_override or "")

    if base_ref_override:
        resolved["base_ref"] = base_ref_override
    if head_ref_override:
        resolved["head_ref"] = head_ref_override
    if head_sha_override:
        if not head_ref_override and head_sha_override != context["head_sha"]:
            raise ValueError("--head-sha-override requires --head-ref-override when it changes the PR head SHA.")
        resolved["head_sha"] = head_sha_override
    return resolved


def resolve_pr_context(
    *,
    pr_number: str,
    repo: str,
    base_ref_override: str = "",
    head_ref_override: str = "",
    head_sha_override: str = "",
) -> dict[str, str]:
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
    if payload.get("isCrossRepository") is not False:
        raise RuntimeError(
            f"Refusing credential-backed validation for fork or unverifiable pull request #{pr_number}."
        )
    context = {
        "pr_number": pr_number,
        "base_ref": _required_ref(payload, "baseRefName", pr_number),
        "head_ref": _required_ref(payload, "headRefName", pr_number),
        "head_sha": _required_ref(payload, "headRefOid", pr_number),
    }
    return _apply_manual_overrides(
        context,
        base_ref_override=base_ref_override,
        head_ref_override=head_ref_override,
        head_sha_override=head_sha_override,
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Resolve GitHub pull request refs for CI workflow_dispatch runs.")
    parser.add_argument("--pr-number", required=True)
    parser.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", ""))
    parser.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT", ""))
    parser.add_argument("--base-ref-override", default="")
    parser.add_argument("--head-ref-override", default="")
    parser.add_argument("--head-sha-override", default="")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if not args.repo:
        raise RuntimeError("--repo or GITHUB_REPOSITORY is required.")
    write_outputs(
        resolve_pr_context(
            pr_number=args.pr_number,
            repo=args.repo,
            base_ref_override=args.base_ref_override,
            head_ref_override=args.head_ref_override,
            head_sha_override=args.head_sha_override,
        ),
        args.github_output,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
