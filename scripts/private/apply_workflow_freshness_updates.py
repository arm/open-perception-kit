#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path


PATCHABLE_STATUSES = {"behind", "different"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Apply workflow action ref updates from a workflow freshness report.",
    )
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument("--report-json", required=True, type=Path)
    parser.add_argument("--skip-ref-check", action="store_true")
    return parser.parse_args()


def load_report(report_json: Path) -> list[dict[str, object]]:
    payload = json.loads(report_json.read_text(encoding="utf-8"))
    entries = payload.get("entries")
    if not isinstance(entries, list):
        raise ValueError(f"Invalid freshness report payload in {report_json}.")
    return [entry for entry in entries if isinstance(entry, dict)]


def parse_usage(usage: str) -> tuple[Path, int]:
    path_str, separator, line_str = usage.rpartition(":")
    if not separator:
        raise ValueError(f"Invalid usage entry '{usage}'.")
    return Path(path_str), int(line_str)


def verify_ref_exists(repository: str, ref: str) -> None:
    result = subprocess.run(
        [
            "git",
            "ls-remote",
            "--exit-code",
            f"https://github.com/{repository}.git",
            ref,
        ],
        check=False,
        text=True,
        capture_output=True,
    )
    if result.returncode != 0:
        raise ValueError(f"Target ref '{repository}@{ref}' does not resolve on GitHub.")


def rewrite_file(
    repo_root: Path,
    path: Path,
    line_numbers: list[int],
    needle: str,
    replacement: str,
) -> int:
    workflow_path = repo_root / path
    lines = workflow_path.read_text(encoding="utf-8").splitlines(keepends=True)
    replacements = 0

    for line_number in sorted(set(line_numbers)):
        index = line_number - 1
        if index < 0 or index >= len(lines):
            raise ValueError(
                f"Usage line {line_number} is out of range for {workflow_path.as_posix()}.",
            )

        if needle in lines[index]:
            lines[index] = lines[index].replace(needle, replacement, 1)
            replacements += 1
            continue

        candidate_indexes = [
            current_index
            for current_index, line in enumerate(lines)
            if needle in line
        ]
        if len(candidate_indexes) != 1:
            raise ValueError(
                f"Could not uniquely locate '{needle}' in {workflow_path.as_posix()} "
                f"for usage line {line_number}.",
            )
        candidate_index = candidate_indexes[0]
        lines[candidate_index] = lines[candidate_index].replace(needle, replacement, 1)
        replacements += 1

    workflow_path.write_text("".join(lines), encoding="utf-8")
    return replacements


def apply_entry(repo_root: Path, entry: dict[str, object]) -> int:
    status = str(entry.get("status") or "")
    latest_ref = str(entry.get("latest_ref") or "")
    repository = str(entry.get("repository") or "")
    current_ref = str(entry.get("current_ref") or "")
    usages = entry.get("usages")

    if status not in PATCHABLE_STATUSES or not latest_ref or not isinstance(usages, list):
        return 0

    needle = f"{repository}@{current_ref}"
    replacement = f"{repository}@{latest_ref}"
    grouped_usages: dict[Path, list[int]] = {}
    for raw_usage in usages:
        path, line_number = parse_usage(str(raw_usage))
        grouped_usages.setdefault(path, []).append(line_number)

    replacements = 0
    for path, line_numbers in grouped_usages.items():
        replacements += rewrite_file(repo_root, path, line_numbers, needle, replacement)
    return replacements


def main() -> int:
    args = parse_args()
    repo_root = args.repo_root.resolve()
    report_json = args.report_json.resolve()
    entries = load_report(report_json)

    replacements = 0
    touched_repositories: list[str] = []
    checked_refs: set[tuple[str, str]] = set()
    for entry in entries:
        status = str(entry.get("status") or "")
        repository = str(entry.get("repository") or "")
        latest_ref = str(entry.get("latest_ref") or "")
        if (
            status in PATCHABLE_STATUSES
            and repository
            and latest_ref
            and not args.skip_ref_check
            and (repository, latest_ref) not in checked_refs
        ):
            verify_ref_exists(repository, latest_ref)
            checked_refs.add((repository, latest_ref))

        entry_replacements = apply_entry(repo_root, entry)
        if entry_replacements:
            replacements += entry_replacements
            touched_repositories.append(str(entry.get("repository") or ""))

    if replacements == 0:
        print("No patchable workflow freshness updates were applied.")
        return 0

    unique_repositories = ", ".join(sorted(set(touched_repositories)))
    print(
        f"Applied {replacements} workflow ref update(s) across "
        f"{len(set(touched_repositories))} action repository/repositories: "
        f"{unique_repositories}",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
