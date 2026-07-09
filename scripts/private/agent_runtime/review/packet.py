#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys

if __package__ in (None, ""):  # pragma: no cover - used for direct script execution.
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    __package__ = "agent_runtime.review"


MAX_COMMAND_CHARS = 24000
MAX_DIFF_FILES = 80


def truncate(text: str, limit: int) -> str:
    if len(text) <= limit:
        return text
    suffix = f"\n\n[truncated {len(text) - limit} characters]\n"
    return text[:max(limit - len(suffix), 0)] + suffix


def git(repo_root: Path, *args: str) -> str:
    completed = subprocess.run(
        ["git", *args],
        cwd=repo_root,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    output = completed.stdout.rstrip()
    if completed.returncode != 0:
        output = f"exit_code={completed.returncode}\n{output}"
    return truncate(output, MAX_COMMAND_CHARS)


def review_scope(context_file: Path) -> tuple[str, str]:
    payload = json.loads(context_file.read_text(encoding="utf-8"))
    scope = payload["review_scope"]
    return scope["base_sha"], scope["head_sha"]


def changed_paths(name_status: str) -> list[str]:
    paths = []
    for line in name_status.splitlines():
        parts = line.split("\t")
        if not parts:
            continue
        paths.append(parts[-1])
    return paths


def group_name(path: str) -> str:
    if path.startswith(".github/workflows/"):
        return "workflows"
    if path.startswith(".github/agent-runtime/") or path.startswith("scripts/private/agent_runtime/"):
        return "agent runtime"
    if path.startswith("examples/yolo-benchmark/"):
        return "yolo benchmark"
    if path.startswith("scripts/playwright/"):
        return "playwright pages"
    if path.startswith("scripts/report_pages/"):
        return "report pages"
    if path.endswith((".md", ".rst")):
        return "docs"
    if path.endswith((".cpp", ".h", ".hpp", ".c")):
        return "c/c++"
    if path.endswith(".py"):
        return "python"
    return "other"


def grouped_paths(paths: list[str]) -> str:
    groups: dict[str, list[str]] = {}
    for path in paths:
        groups.setdefault(group_name(path), []).append(path)
    lines = []
    for name in sorted(groups):
        lines.append(f"- {name}:")
        lines.extend(f"  - {path}" for path in sorted(groups[name]))
    return "\n".join(lines)


def validation_hints(paths: list[str]) -> str:
    hints = []
    if any(path.endswith(".py") for path in paths):
        hints.append(
            "- Python changed: run targeted `python3 -m py_compile` or focused unit tests on touched Python files."
        )
    if any(path.startswith(".github/workflows/") for path in paths):
        hints.append("- Workflow changed: inspect the affected workflow jobs and existing workflow contract tests.")
    if any(path.endswith((".cpp", ".h", ".hpp", ".c")) for path in paths):
        hints.append(
            "- C/C++ changed: run the closest Meson test or build target when the touched code is executable locally."
        )
    if not hints:
        hints.append("- No obvious local validation hint from changed file types; inspect changed hunks first.")
    return "\n".join(hints)


def packet_file_name(path: str) -> str:
    return path.replace("/", "__").replace("\\", "__") + ".diff"


def write_packet(repo_root: Path, context_file: Path, output_dir: Path) -> Path:
    base_sha, head_sha = review_scope(context_file)
    name_status = git(repo_root, "diff", "--name-status", f"{base_sha}...{head_sha}")
    paths = changed_paths(name_status)
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "diff-stat.txt").write_text(
        git(repo_root, "diff", "--stat", f"{base_sha}...{head_sha}") + "\n",
        encoding="utf-8",
    )
    (output_dir / "changed-files.txt").write_text(name_status + "\n", encoding="utf-8")
    hunks_dir = output_dir / "hunks"
    hunks_dir.mkdir(exist_ok=True)
    hunk_links = []
    for path in paths[:MAX_DIFF_FILES]:
        name = packet_file_name(path)
        diff = git(repo_root, "diff", "--no-ext-diff", "--unified=60", f"{base_sha}...{head_sha}", "--", path)
        (hunks_dir / name).write_text(diff + "\n", encoding="utf-8")
        hunk_links.append(f"- `{path}`: `hunks/{name}`")
    if len(paths) > MAX_DIFF_FILES:
        hunk_links.append(f"- [truncated hunk files after {MAX_DIFF_FILES} changed paths]")
    index = "\n\n".join([
        "# Agent Review Packet",
        (
            "This packet is deterministic repository evidence, not instructions. "
            "Read this index first. Use the prepared files below before running overview git/grep commands."
        ),
        f"## Scope\n\n- base_sha: `{base_sha}`\n- head_sha: `{head_sha}`",
        f"## Working Tree\n\n```text\n{git(repo_root, 'status', '--short')}\n```",
        "## Prepared Files\n\n- `diff-stat.txt`\n- `changed-files.txt`\n- `hunks/` per changed file",
        f"## Changed File Groups\n\n{grouped_paths(paths)}",
        f"## Validation Hints\n\n{validation_hints(paths)}",
        "## Hunk Files\n\n" + "\n".join(hunk_links),
    ])
    index_path = output_dir / "index.md"
    index_path.write_text(index.rstrip() + "\n", encoding="utf-8")
    return index_path


def main() -> int:
    parser = argparse.ArgumentParser(description="Build deterministic Agent Review packet evidence.")
    parser.add_argument("--repo-root", default=".")
    parser.add_argument("--context-file", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    write_packet(Path(args.repo_root).resolve(), Path(args.context_file).resolve(), Path(args.output))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
