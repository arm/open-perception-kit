#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import os
import subprocess
from pathlib import Path


DEFAULT_TEMPLATE_PATH = Path(".github/agent-runtime/review/prompts/review.md.in")
MAX_PR_BODY_CHARS = 8000


def git_output(args: list[str]) -> str:
    completed = subprocess.run(
        ["git", *args],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return completed.stdout.strip()


def fill_empty(value: str, fallback: str) -> str:
    return value if value else fallback


def normalize_text(value: str) -> str:
    return value.replace("\n", " ")


def bounded_text(value: str, max_chars: int) -> str:
    text = value.strip()
    if len(text) <= max_chars:
        return text
    omitted = len(text) - max_chars
    return f"{text[:max_chars].rstrip()}\n\n[truncated {omitted} pull request body characters]"


def blockquote_text(value: str, fallback: str = "(not provided)") -> str:
    text = bounded_text(value, MAX_PR_BODY_CHARS) or fallback
    return "\n".join(f"> {line}" if line else ">" for line in text.splitlines())


def default_repository() -> str:
    configured = os.environ.get("REVIEW_REPOSITORY") or os.environ.get("GITHUB_REPOSITORY")
    if configured:
        return configured
    return Path(git_output(["rev-parse", "--show-toplevel"])).name


def render_prompt(*, output_path: Path, template_path: Path = DEFAULT_TEMPLATE_PATH) -> None:
    base_ref = os.environ.get("REVIEW_BASE_REF", "origin/main")
    head_ref = os.environ.get("REVIEW_HEAD_REF", "HEAD")
    base_sha = os.environ.get("REVIEW_BASE_SHA", "")
    head_sha = os.environ.get("REVIEW_HEAD_SHA", "")

    if not head_sha:
        head_sha = git_output(["rev-parse", head_ref])
    if not base_sha:
        base_sha = git_output(["merge-base", base_ref, head_ref])

    replacements = {
        "@@REPOSITORY@@": fill_empty(default_repository(), "(not provided)"),
        "@@BASE_REF@@": fill_empty(base_ref, "(not provided)"),
        "@@BASE_SHA@@": fill_empty(base_sha, "(not provided)"),
        "@@HEAD_SHA@@": fill_empty(head_sha, "(not provided)"),
        "@@PR_NUMBER@@": fill_empty(os.environ.get("REVIEW_PR_NUMBER", ""), "(not a pull request run)"),
        "@@PR_TITLE@@": fill_empty(normalize_text(os.environ.get("REVIEW_PR_TITLE", "")), "(not provided)"),
        "@@PR_URL@@": fill_empty(os.environ.get("REVIEW_PR_URL", ""), "(not provided)"),
        "@@PR_BODY_BLOCKQUOTE@@": blockquote_text(os.environ.get("REVIEW_PR_BODY", "")),
    }

    rendered = template_path.read_text(encoding="utf-8")
    for token, value in replacements.items():
        rendered = rendered.replace(token, value)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(rendered, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Render the Agent Review prompt.")
    parser.add_argument("--output", required=True, help="Rendered prompt output path.")
    parser.add_argument(
        "--template",
        default=str(DEFAULT_TEMPLATE_PATH),
        help="Review prompt template path.",
    )
    args = parser.parse_args()

    render_prompt(output_path=Path(args.output), template_path=Path(args.template))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
