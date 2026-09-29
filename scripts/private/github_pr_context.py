#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from __future__ import annotations

import argparse
import os
from pathlib import Path
import urllib.parse

from github_actions import read_pr_details
from github_api import github_api_json


def write_outputs(values: dict[str, str], output_path: str | None = None) -> None:
    target = output_path or os.environ.get("GITHUB_OUTPUT")
    if not target:
        raise ValueError("GITHUB_OUTPUT is not set and no explicit output path was provided.")
    with Path(target).open("a", encoding="utf-8") as output_file:
        for key, value in values.items():
            output_file.write(f"{key}={value}\n")


def _required_ref(value: object, pr_number: str) -> str:
    if not isinstance(value, str) or not value:
        raise RuntimeError(f"Incomplete pull request refs for PR #{pr_number}.")
    return value


def _resolve_ref_sha(repo: str, ref: str) -> str:
    payload = github_api_json(f"repos/{repo}/commits/{urllib.parse.quote(ref, safe='')}")
    sha = payload.get("sha") if isinstance(payload, dict) else None
    if not isinstance(sha, str) or not sha:
        raise RuntimeError(f"Unable to resolve base ref '{ref}'.")
    return sha


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
        if not head_sha_override:
            resolved["head_sha"] = ""
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
    details = read_pr_details(pr_number, repository=repo)
    context = {
        "pr_number": pr_number,
        "base_ref": _required_ref(details.get("target_branch"), pr_number),
        "base_sha": _required_ref(details.get("base_sha"), pr_number),
        "head_ref": _required_ref(details.get("head_branch"), pr_number),
        "head_sha": _required_ref(details.get("head_sha"), pr_number),
    }
    resolved = _apply_manual_overrides(
        context,
        base_ref_override=base_ref_override,
        head_ref_override=head_ref_override,
        head_sha_override=head_sha_override,
    )
    if base_ref_override:
        resolved["base_sha"] = _resolve_ref_sha(repo, resolved["base_ref"])
    return resolved


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
