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
import json
import os
import re
import sys
from collections import Counter
from pathlib import Path
from urllib.parse import quote

if __package__ in (None, ""):  # pragma: no cover - used for direct script execution.
    sys.path.insert(0, str(Path(__file__).resolve().parent))

from github_api import (  # noqa: E402
    github_api_base_url,
    github_api_json_or_empty,
    github_api_query_endpoint,
)

USES_RE = re.compile(r"^\s*(?:-\s*)?uses:\s*['\"]?(?P<uses>[^'\"\s#]+)")
SEMVER_RE = re.compile(r"^v?(?P<major>\d+)(?:\.(?P<minor>\d+))?(?:\.(?P<patch>\d+))?$")
SHA_RE = re.compile(r"^[0-9a-f]{7,40}$", re.IGNORECASE)
DEFAULT_SERVER_URL = "https://github.com"
STATUS_ORDER = {
    "behind": 0,
    "different": 1,
    "pinned": 2,
    "ahead": 3,
    "tracking": 4,
    "up-to-date": 5,
    "unknown": 6,
}
BEHIND_STATUSES = {"behind", "different"}
REVIEW_STATUSES = {"pinned", "ahead", "unknown"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Render a small workflow dependency freshness report.",
    )
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument("--markdown-output", required=True, type=Path)
    parser.add_argument("--json-output", type=Path)
    parser.add_argument("--summary-limit", type=int, default=6)
    parser.add_argument("--api-url", default=github_api_base_url())
    parser.add_argument(
        "--server-url",
        default=os.environ.get("GITHUB_SERVER_URL", DEFAULT_SERVER_URL),
    )
    return parser.parse_args()


def parse_uses_reference(raw_uses: str) -> tuple[str, str] | None:
    if raw_uses.startswith("./") or raw_uses.startswith("docker://") or "${{" in raw_uses:
        return None
    if "@" not in raw_uses:
        return None
    action_path, current_ref = raw_uses.rsplit("@", 1)
    parts = action_path.split("/")
    if len(parts) < 2:
        return None
    return "/".join(parts[:2]), current_ref


def parse_semver(ref: str) -> tuple[int, int, int, int] | None:
    match = SEMVER_RE.fullmatch(ref)
    if not match:
        return None
    precision = 1
    if match.group("minor") is not None:
        precision = 2
    if match.group("patch") is not None:
        precision = 3
    return (
        int(match.group("major")),
        int(match.group("minor") or 0),
        int(match.group("patch") or 0),
        precision,
    )


def collect_entries(repo_root: Path) -> list[dict[str, object]]:
    workflows_root = repo_root / ".github" / "workflows"
    entries: dict[tuple[str, str], dict[str, object]] = {}
    workflow_paths = sorted(workflows_root.glob("*.yml")) + sorted(workflows_root.glob("*.yaml"))
    for workflow_path in workflow_paths:
        rel_path = workflow_path.relative_to(repo_root).as_posix()
        for line_number, line in enumerate(
            workflow_path.read_text(encoding="utf-8", errors="replace").splitlines(),
            start=1,
        ):
            match = USES_RE.match(line)
            if not match:
                continue
            parsed = parse_uses_reference(match.group("uses"))
            if parsed is None:
                continue
            repository, current_ref = parsed
            key = (repository, current_ref)
            if key not in entries:
                entries[key] = {
                    "repository": repository,
                    "current_ref": current_ref,
                    "usages": [],
                }
            entries[key]["usages"].append(f"{rel_path}:{line_number}")
    return [entries[key] for key in sorted(entries)]


def fetch_latest_ref(token: str | None, repository: str) -> tuple[str, str]:
    repository_path = quote(repository, safe="/")
    payload = github_api_json_or_empty(
        f"repos/{repository_path}/releases/latest",
        token=token,
    )
    latest_release = str(payload.get("tag_name") or "").strip() if isinstance(payload, dict) else ""
    if latest_release:
        return latest_release, "release"
    payload = github_api_json_or_empty(
        github_api_query_endpoint(
            f"repos/{repository_path}/tags",
            {"per_page": 1},
        ),
        token=token,
    )
    if isinstance(payload, list) and payload:
        first_tag = payload[0] if isinstance(payload[0], dict) else {}
        latest_tag = str(first_tag.get("name") or "").strip()
        if latest_tag:
            return latest_tag, "tag"
    return "", ""


def classify_freshness(current_ref: str, latest_ref: str) -> str:
    if not latest_ref:
        return "unknown"
    if current_ref == latest_ref:
        return "up-to-date"
    if SHA_RE.fullmatch(current_ref):
        return "pinned"
    current_semver = parse_semver(current_ref)
    latest_semver = parse_semver(latest_ref)
    if current_semver and latest_semver:
        if current_semver[:3] == latest_semver[:3]:
            return "up-to-date"
        if (
            current_semver[3] < 3
            and current_semver[0] == latest_semver[0]
            and (current_semver[3] == 1 or current_semver[1] == latest_semver[1])
        ):
            return "tracking"
        if current_semver[:3] < latest_semver[:3]:
            return "behind"
        if current_semver[:3] > latest_semver[:3]:
            return "ahead"
    return "different"


def repository_url(server_url: str, repository: str) -> str:
    return f"{server_url.rstrip('/')}/{repository}"


def ref_url(server_url: str, repository: str, ref: str, source: str) -> str:
    if not ref:
        return ""
    base = repository_url(server_url, repository)
    encoded_ref = quote(ref, safe="")
    if SHA_RE.fullmatch(ref):
        return f"{base}/commit/{encoded_ref}"
    if source == "release":
        return f"{base}/releases/tag/{encoded_ref}"
    return f"{base}/tree/{encoded_ref}"


def compare_url(server_url: str, repository: str, current_ref: str, latest_ref: str) -> str:
    if not current_ref or not latest_ref:
        return ""
    base = repository_url(server_url, repository)
    return f"{base}/compare/{quote(current_ref, safe='')}...{quote(latest_ref, safe='')}"


def markdown_link(label: str, url: str) -> str:
    if not url:
        return label
    return f"[{label}]({url})"


def usage_summary(usages: list[str]) -> str:
    counts = Counter(item.split(":", 1)[0] for item in usages)
    ordered = sorted(counts.items(), key=lambda item: (-item[1], item[0]))
    return ", ".join(f"`{path}` ({count})" for path, count in ordered)


def render_markdown(entries: list[dict[str, object]]) -> str:
    counts = Counter(str(entry["status"]) for entry in entries)
    lines = [
        "# Workflow Dependency Freshness",
        "",
        "Simple snapshot of external GitHub Actions `uses:` references.",
        "",
        "| Metric | Value |",
        "| --- | ---: |",
        f"| Tracked refs | {len(entries)} |",
        f"| Not matching latest | {sum(counts[status] for status in BEHIND_STATUSES)} |",
        f"| Needs review | {sum(counts[status] for status in REVIEW_STATUSES)} |",
        "",
        "| Repository | Current | Latest | Status | Compare | Used in |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for entry in entries:
        current_label = markdown_link(
            f"`{entry['current_ref']}`",
            str(entry["current_url"]),
        )
        latest_label = "n/a"
        if entry["latest_ref"]:
            latest_label = markdown_link(
                f"`{entry['latest_ref']}`",
                str(entry["latest_url"]),
            )
        compare_label = "-"
        if entry["compare_url"]:
            compare_label = markdown_link("compare", str(entry["compare_url"]))
        lines.append(
            "| "
            f"{markdown_link(str(entry['repository']), str(entry['repository_url']))} | "
            f"{current_label} | "
            f"{latest_label} | "
            f"{entry['status']} | "
            f"{compare_label} | "
            f"{usage_summary(entry['usages'])} |"
        )
    return "\n".join(lines).rstrip() + "\n"


def render_summary(entries: list[dict[str, object]], limit: int) -> str:
    highlights = [
        entry for entry in entries
        if str(entry["status"]) in BEHIND_STATUSES or str(entry["status"]) in REVIEW_STATUSES
    ][:limit]
    lines = [
        "### Workflow dependency freshness",
        "",
        f"- Tracked refs: {len(entries)}",
        f"- Not matching latest: {sum(str(entry['status']) in BEHIND_STATUSES for entry in entries)}",
        f"- Needs review: {sum(str(entry['status']) in REVIEW_STATUSES for entry in entries)}",
        "- Full report is available in the `workflow-dependency-freshness` artifact.",
        "",
    ]
    if not highlights:
        lines.append("- No freshness issues detected.")
    else:
        for entry in highlights:
            latest_ref = entry["latest_ref"] or "n/a"
            lines.append(
                f"- {entry['repository']}: `{entry['current_ref']}` -> `{latest_ref}` "
                f"({entry['status']})"
            )
    return "\n".join(lines).rstrip() + "\n"


def render_json(entries: list[dict[str, object]]) -> str:
    payload = {
        "tracked_refs": len(entries),
        "behind_latest": sum(str(entry["status"]) in BEHIND_STATUSES for entry in entries),
        "needs_review": sum(str(entry["status"]) in REVIEW_STATUSES for entry in entries),
        "entries": entries,
    }
    return json.dumps(payload, indent=2, sort_keys=True) + "\n"


def main() -> int:
    args = parse_args()
    os.environ["GITHUB_API_URL"] = args.api_url
    token = os.environ.get("GITHUB_TOKEN")
    entries = collect_entries(args.repo_root.resolve())
    cache: dict[str, tuple[str, str]] = {}
    for entry in entries:
        repository = str(entry["repository"])
        if repository not in cache:
            cache[repository] = fetch_latest_ref(token, repository)
        latest_ref, latest_source = cache[repository]
        current_ref = str(entry["current_ref"])
        entry["latest_ref"] = latest_ref
        entry["latest_source"] = latest_source
        entry["status"] = classify_freshness(current_ref, latest_ref)
        entry["repository_url"] = repository_url(args.server_url, repository)
        entry["current_url"] = ref_url(args.server_url, repository, current_ref, "")
        entry["latest_url"] = ref_url(args.server_url, repository, latest_ref, latest_source)
        entry["compare_url"] = compare_url(args.server_url, repository, current_ref, latest_ref)
    entries.sort(key=lambda entry: (STATUS_ORDER[str(entry["status"])], str(entry["repository"])))
    markdown = render_markdown(entries)
    args.markdown_output.parent.mkdir(parents=True, exist_ok=True)
    args.markdown_output.write_text(markdown, encoding="utf-8")
    if args.json_output is not None:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(render_json(entries), encoding="utf-8")
    sys.stdout.write(render_summary(entries, args.summary_limit))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        raise SystemExit(2)
