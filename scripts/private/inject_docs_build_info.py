#!/usr/bin/env python3
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

BEGIN_MARKER = "<!-- BEGIN GENERATED DOCS BUILD INFO -->"
END_MARKER = "<!-- END GENERATED DOCS BUILD INFO -->"


def git(*args: str) -> str:
    completed = subprocess.run(
        ["git", *args],
        check=True,
        capture_output=True,
        text=True,
    )
    return completed.stdout.strip()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Inject published build metadata into a docs markdown file.",
    )
    parser.add_argument(
        "--target",
        type=Path,
        required=True,
        help="Markdown file to update in place.",
    )
    return parser.parse_args()


def format_build_info() -> str:
    full_sha = git("rev-parse", "HEAD")
    return "\n".join(
        [
            BEGIN_MARKER,
            f'<div hidden data-published-commit="{full_sha}"></div>',
            END_MARKER,
        ]
    )


def replace_build_info_block(text: str, replacement: str) -> str:
    pattern = re.compile(
        rf"{re.escape(BEGIN_MARKER)}.*?{re.escape(END_MARKER)}",
        re.DOTALL,
    )
    if not pattern.search(text):
        raise ValueError(
            f"Could not find generated build-info markers: {BEGIN_MARKER} ... {END_MARKER}",
        )
    return pattern.sub(replacement, text, count=1)


def main() -> int:
    args = parse_args()
    target = args.target.resolve()

    text = target.read_text(encoding="utf-8")
    updated = replace_build_info_block(text, format_build_info())
    target.write_text(updated, encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, subprocess.CalledProcessError, ValueError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        raise SystemExit(2)
