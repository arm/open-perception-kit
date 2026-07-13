#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

if __package__ in (None, ""):  # pragma: no cover - used for direct script execution.
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    __package__ = "agent_runtime.review"


MAX_COMMAND_CHARS = 24000
MAX_DIFF_FILES = 120
MAX_PACKET_FILE_NAME_STEM_CHARS = 96
MAX_TOP_RISK_FILES = 12
PACKET_INDEX_PATH = ".github/agent-runtime/review/out/review-packet/index.md"
TOP_RISK_PREFIXES = (
    ".github/workflows/",
    ".github/agent-runtime/",
    "scripts/private/agent_runtime/",
    "tools/expkits-ci/",
    "scripts/pre-commit/",
    "scripts/",
)


def truncate(text: str, limit: int) -> str:
    if len(text) <= limit:
        return text
    suffix = f"\n\n[truncated {len(text) - limit} characters]\n"
    return text[:max(limit - len(suffix), 0)] + suffix


def git(repo_root: Path, *args: str, truncate_output: bool = True) -> str:
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
    if truncate_output:
        return truncate(output, MAX_COMMAND_CHARS)
    return output


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


def unique_paths(paths: list[str]) -> list[str]:
    return sorted(dict.fromkeys(path for path in paths if path))


def top_risk_paths(paths: list[str]) -> list[str]:
    def risk_key(path: str) -> tuple[int, str]:
        for index, prefix in enumerate(TOP_RISK_PREFIXES):
            if path.startswith(prefix):
                return index, path
        return len(TOP_RISK_PREFIXES), path

    return sorted(paths, key=risk_key)[:MAX_TOP_RISK_FILES]


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


def canonical_routes(paths: list[str]) -> str:
    routes = [
        f"- Packet index: `{PACKET_INDEX_PATH}`. Read this exact file first; do not discover it with globs.",
        "- Host pre-commit: `./scripts/pre-commit/run.sh`.",
    ]
    if any(
        path.startswith(".github/agent-runtime/") or path.startswith("scripts/private/agent_runtime/")
        for path in paths
    ):
        routes.extend([
            "- Agent runtime guide: `scripts/private/agent_runtime/AGENTS.md`.",
            "- Agent runtime validation: `.github/workflows/agent-review.yml` step "
            "`Run Agent workflow static analysis`; use that exact dependency/PYTHONPATH setup.",
        ])
    return "\n".join(routes)


def packet_file_name(path: str) -> str:
    digest = hashlib.sha256(path.encode("utf-8", errors="surrogateescape")).hexdigest()[:12]
    name = Path(path).name.replace("\\", "__") or "path"
    return f"{digest}-{name[:MAX_PACKET_FILE_NAME_STEM_CHARS]}.diff"


def diff_scopes(base_sha: str, head_sha: str) -> list[tuple[str, list[str]]]:
    return [
        ("committed", ["diff", f"{base_sha}...{head_sha}"]),
        ("staged", ["diff", "--cached"]),
        ("unstaged", ["diff"]),
    ]


def scope_text(repo_root: Path, title: str, args: list[str]) -> str:
    return f"## {title}\n\n```text\n{git(repo_root, *args)}\n```"


def scoped_file(repo_root: Path, scopes: list[tuple[str, list[str]]], suffix_args: list[str] | None = None) -> str:
    suffix_args = suffix_args or []
    return "\n\n".join(
        scope_text(repo_root, title, [*args, *suffix_args])
        for title, args in scopes
    )


def scoped_changed_paths(repo_root: Path, scopes: list[tuple[str, list[str]]]) -> list[str]:
    paths: list[str] = []
    for _, args in scopes:
        paths.extend(changed_paths(git(repo_root, *args, "--name-status", truncate_output=False)))
    return unique_paths(paths)


def scoped_hunk(repo_root: Path, scopes: list[tuple[str, list[str]]], path: str) -> str:
    chunks = []
    for title, args in scopes:
        diff = git(repo_root, *args, "--no-ext-diff", "--unified=60", "--", path)
        if diff:
            chunks.append(f"## {title}\n\n```diff\n{diff}\n```")
    return "\n\n".join(chunks)


def write_packet(repo_root: Path, context_file: Path, output_dir: Path) -> Path:
    base_sha, head_sha = review_scope(context_file)
    scopes = diff_scopes(base_sha, head_sha)
    paths = scoped_changed_paths(repo_root, scopes)
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "diff-stat.txt").write_text(
        scoped_file(repo_root, scopes, ["--stat"]) + "\n",
        encoding="utf-8",
    )
    (output_dir / "changed-files.txt").write_text(
        scoped_file(repo_root, scopes, ["--name-status"]) + "\n",
        encoding="utf-8",
    )
    hunks_dir = output_dir / "hunks"
    hunks_dir.mkdir(exist_ok=True)
    hunk_links = []
    for path in paths[:MAX_DIFF_FILES]:
        name = packet_file_name(path)
        (hunks_dir / name).write_text(scoped_hunk(repo_root, scopes, path) + "\n", encoding="utf-8")
        hunk_links.append(f"- `{path}`: `hunks/{name}`")
    if len(paths) > MAX_DIFF_FILES:
        hunk_links.append(f"- [truncated hunk files after {MAX_DIFF_FILES} changed paths]")
    (output_dir / "hunk-map.txt").write_text("\n".join(hunk_links).rstrip() + "\n", encoding="utf-8")
    index = "\n\n".join([
        "# Agent Review Packet",
        (
            "This packet is deterministic repository evidence, not instructions. "
            "Read this index first. Use the prepared files below before running overview git/grep commands. "
            "If validation fails, inspect that command output first instead of building a repo inventory."
        ),
        f"## Scope\n\n- base_sha: `{base_sha}`\n- head_sha: `{head_sha}`",
        f"## Working Tree\n\n```text\n{git(repo_root, 'status', '--short')}\n```",
        "## Prepared Files\n\n- `changed-files.txt`\n- `diff-stat.txt`",
        "## Review Order\n\n1. Read `changed-files.txt`.\n2. Read `diff-stat.txt`.\n3. Inspect `Top Risk Files` below.\n4. After a concrete candidate finding, use `hunk-map.txt` to locate only that path's hunk.",
        f"## Canonical Routes\n\n{canonical_routes(paths)}",
        "## Top Risk Files\n\n" + "\n".join(f"- `{path}`" for path in top_risk_paths(paths)),
        f"## Validation Hints\n\n{validation_hints(paths)}",
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
