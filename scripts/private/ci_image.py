#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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
from datetime import datetime, timedelta, timezone
import json
import os
import re
import subprocess
import sys


CI_IMAGE_TAG_PATTERN = re.compile(r"opk-ci-(?:run|pr)-[0-9]+")
DEV_IMAGE_TAG_PATTERN = re.compile(r"sha-[0-9a-f]{40}")


def github_repository() -> str:
    value = os.environ.get("GITHUB_REPOSITORY", "").strip().lower()
    if value.count("/") != 1:
        raise RuntimeError("GITHUB_REPOSITORY must contain owner/repository.")
    return value


def run(command: list[str], *, capture_output: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        check=True,
        text=True,
        stdout=subprocess.PIPE if capture_output else None,
        stderr=subprocess.PIPE if capture_output else None,
    )


def version_tags(version: dict[str, object]) -> list[str]:
    metadata = version.get("metadata", {})
    if not isinstance(metadata, dict):
        raise ValueError("Package version metadata must be an object.")
    container = metadata.get("container", {})
    if not isinstance(container, dict):
        raise ValueError("Package version container metadata must be an object.")
    tags = container.get("tags", [])
    if not isinstance(tags, list) or any(not isinstance(tag, str) for tag in tags):
        raise ValueError("Package version tags must be strings.")
    return tags


def versions_to_delete(versions: list[dict[str, object]], keep: int) -> list[int]:
    if keep < 0:
        raise ValueError("Retention count cannot be negative.")
    sha_versions = sorted(
        (
            version
            for version in versions
            if any(
                DEV_IMAGE_TAG_PATTERN.fullmatch(tag)
                for tag in version_tags(version)
            )
        ),
        key=lambda version: str(version.get("created_at", "")),
        reverse=True,
    )
    expired_ids = {version.get("id") for version in sha_versions[keep:]}
    deletion_ids: list[int] = []
    for version in versions:
        version_id = version.get("id")
        if not isinstance(version_id, int):
            raise ValueError("Package version id must be an integer.")
        tags = version_tags(version)
        if not tags or (
            version_id in expired_ids
            and all(DEV_IMAGE_TAG_PATTERN.fullmatch(tag) for tag in tags)
        ):
            deletion_ids.append(version_id)
    return deletion_ids


def stale_ci_versions_to_delete(
    versions: list[dict[str, object]], cutoff: datetime
) -> list[int]:
    if cutoff.tzinfo is None:
        raise ValueError("CI image retention cutoff must include a timezone.")
    deletion_ids: list[int] = []
    for version in versions:
        version_id = version.get("id")
        if not isinstance(version_id, int):
            raise ValueError("Package version id must be an integer.")
        updated_at_value = version.get("updated_at") or version.get("created_at")
        if not isinstance(updated_at_value, str):
            raise ValueError("Package version timestamp must be a string.")
        updated_at = datetime.fromisoformat(updated_at_value.replace("Z", "+00:00"))
        if updated_at.tzinfo is None:
            raise ValueError("Package version timestamp must include a timezone.")
        tags = version_tags(version)
        if updated_at < cutoff and (
            not tags or all(CI_IMAGE_TAG_PATTERN.fullmatch(tag) for tag in tags)
        ):
            deletion_ids.append(version_id)
    return deletion_ids


def retain_dev_images(keep: int) -> list[int]:
    owner, repository = github_repository().split("/", 1)
    package = f"{repository}-dev"
    endpoint = f"orgs/{owner}/packages/container/{package}/versions"
    result = run(
        ["gh", "api", "--paginate", "--jq", ".[]", f"{endpoint}?per_page=100"],
        capture_output=True,
    )
    versions = [json.loads(line) for line in result.stdout.splitlines()]
    if any(not isinstance(version, dict) for version in versions):
        raise ValueError("GitHub package versions response contains an invalid version.")
    deletion_ids = versions_to_delete(versions, keep)
    for version_id in deletion_ids:
        run(["gh", "api", "--method", "DELETE", f"{endpoint}/{version_id}"])
    print(f"Deleted {len(deletion_ids)} superseded {package} version(s).")
    return deletion_ids


def retain_ci_images(hours: int) -> list[int]:
    if hours <= 0:
        raise ValueError("CI image retention must be positive.")
    owner, repository = github_repository().split("/", 1)
    package = f"{repository}-ci"
    endpoint = f"orgs/{owner}/packages/container/{package}/versions"
    result = run(
        ["gh", "api", "--paginate", "--jq", ".[]", f"{endpoint}?per_page=100"],
        capture_output=True,
    )
    versions = [json.loads(line) for line in result.stdout.splitlines()]
    if any(not isinstance(version, dict) for version in versions):
        raise ValueError("GitHub package versions response contains an invalid version.")
    cutoff = datetime.now(timezone.utc) - timedelta(hours=hours)
    deletion_ids = stale_ci_versions_to_delete(versions, cutoff)
    for version_id in deletion_ids:
        run(["gh", "api", "--method", "DELETE", f"{endpoint}/{version_id}"])
    print(f"Deleted {len(deletion_ids)} stale {package} image version(s).")
    return deletion_ids


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Retain recent OPK container images.")
    subparsers = parser.add_subparsers(dest="command", required=True)
    retain_dev_parser = subparsers.add_parser("retain-dev")
    retain_dev_parser.add_argument("--keep", type=int, default=20)
    retain_ci_parser = subparsers.add_parser("retain-ci")
    retain_ci_parser.add_argument("--hours", type=int, default=24)
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.command == "retain-dev":
        retain_dev_images(args.keep)
    else:
        retain_ci_images(args.hours)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        print(f"CI image error: {error}", file=sys.stderr)
        raise SystemExit(1) from error
