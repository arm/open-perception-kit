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
"""Store the current Valgrind summary for each target branch in Artifactory."""

import argparse
import base64
import hashlib
import json
import os
import re
import runpy
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path


ACTIVE_STATUSES = {"queued", "in_progress", "requested", "waiting", "pending"}
WORKFLOW_REF = os.environ.get("VALGRIND_BASELINE_WORKFLOW_REF", "develop")
WORKFLOW_NAME = "opk-ci.yml"
ARTIFACTORY_BASE_URL = (
    "https://artifactory.arm.com/artifactory/"
    "ai-expkits-internal.opk-ci/ci/valgrind-baselines"
)
SUMMARY_NAME = "valgrind-error-summary.xml"
NEWEST_REFERENCE_SHA_ATTRIBUTE = "newest-reference-sha"


def gh(*args: str) -> str:
    return subprocess.run(
        ["gh", *args],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def gh_json(*args: str):
    return json.loads(gh(*args))


def required_env(name: str) -> str:
    value = os.environ.get(name, "")
    if not value:
        raise RuntimeError(f"Missing {name}")
    return value


def validate_sha(sha: str) -> None:
    if not re.fullmatch(r"[0-9a-f]{40}", sha):
        raise ValueError(f"Invalid Git commit SHA: {sha}")


def validate_branch_name(branch: str) -> None:
    if not re.fullmatch(r"[A-Za-z0-9._/-]+", branch) or any(
        part in {"", ".", ".."} for part in branch.split("/")
    ):
        raise ValueError(f"Invalid Git branch: {branch}")


def target_branch_name() -> str:
    branch = required_env("TARGET_BRANCH_NAME")
    validate_branch_name(branch)
    return branch


def get_target_branch_head_sha(branch: str) -> str:
    validate_branch_name(branch)
    return gh(
        "api",
        f"repos/{required_env('GITHUB_REPOSITORY')}/git/ref/heads/{branch}",
        "--jq",
        ".object.sha",
    )


def list_backfill_runs():
    return gh_json(
        "run",
        "list",
        "--repo",
        required_env("GITHUB_REPOSITORY"),
        "--workflow",
        WORKFLOW_NAME,
        "--branch",
        WORKFLOW_REF,
        "--event",
        "workflow_dispatch",
        "--limit",
        "20",
        "--json",
        "databaseId,displayTitle,status",
    )


def find_active_run(branch: str, target_sha: str):
    title = f"Valgrind baseline {branch}@{target_sha}"
    for run in list_backfill_runs():
        if run.get("displayTitle") == title and run.get("status") in ACTIVE_STATUSES:
            return int(run["databaseId"])
    return None


def summary_digest(path: Path) -> str:
    compare = runpy.run_path(
        str(Path(__file__).with_name("compare-valgrind-results.py"))
    )
    summary = compare["load_repository_summary"](path)
    payload = json.dumps(sorted(summary.items()), separators=(",", ":"))
    return hashlib.sha256(payload.encode()).hexdigest()


def summary_digest_bytes(payload: bytes) -> str:
    with tempfile.NamedTemporaryFile() as summary:
        summary.write(payload)
        summary.flush()
        return summary_digest(Path(summary.name))


def pack_baseline(target_sha: str, payload: bytes) -> bytes:
    validate_sha(target_sha)
    root = ET.fromstring(payload)
    if root.tag != "valgrindoutput":
        raise ValueError(f"Invalid Valgrind summary root: {root.tag}")
    root.set(NEWEST_REFERENCE_SHA_ATTRIBUTE, target_sha)
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def unpack_baseline(payload: bytes) -> tuple[str, bytes]:
    root = ET.fromstring(payload)
    if root.tag != "valgrindoutput":
        raise ValueError(f"Invalid Valgrind summary root: {root.tag}")
    newest_reference_sha = root.attrib.pop(NEWEST_REFERENCE_SHA_ATTRIBUTE, "")
    validate_sha(newest_reference_sha)
    summary = ET.tostring(root, encoding="utf-8", xml_declaration=True)
    return newest_reference_sha, summary


def artifactory_request(branch: str, *, data: bytes | None = None):
    validate_branch_name(branch)
    user = required_env("ARTIFACTORY_USER")
    token = required_env("ARTIFACTORY_TOKEN")

    credentials = base64.b64encode(f"{user}:{token}".encode()).decode()
    request = urllib.request.Request(
        f"{ARTIFACTORY_BASE_URL}/{branch}/{SUMMARY_NAME}",
        data=data,
        method="PUT" if data is not None else "GET",
    )
    request.add_header("Authorization", f"Basic {credentials}")
    if data is not None:
        request.add_header("Content-Type", "application/xml")
        request.add_header("X-Checksum-Sha256", hashlib.sha256(data).hexdigest())
    return urllib.request.urlopen(request, timeout=30)


def read_remote_baseline(branch: str) -> tuple[str, bytes] | None:
    try:
        with artifactory_request(branch) as response:
            payload = response.read()
    except urllib.error.HTTPError as error:
        if error.code == 404:
            return None
        raise

    try:
        return unpack_baseline(payload)
    except (ET.ParseError, ValueError):
        return None


def baseline_matches(
    baseline: tuple[str, bytes] | None,
    target_sha: str,
    checksum: str,
) -> bool:
    return (
        baseline is not None
        and baseline[0] == target_sha
        and summary_digest_bytes(baseline[1]) == checksum
    )


def reject_same_sha_conflict(
    baseline: tuple[str, bytes] | None,
    branch: str,
    target_sha: str,
) -> None:
    if baseline is not None and baseline[0] == target_sha:
        raise RuntimeError(
            f"Conflicting Artifactory Valgrind baseline already exists "
            f"for {branch}@{target_sha}."
        )


def download_baseline(
    branch: str,
    target_sha: str,
    output_dir: Path,
    *,
    quiet: bool = False,
) -> int:
    validate_branch_name(branch)
    validate_sha(target_sha)
    summary = output_dir / SUMMARY_NAME
    summary.unlink(missing_ok=True)
    baseline = read_remote_baseline(branch)
    if baseline is None:
        if not quiet:
            print(
                f"No valid Artifactory Valgrind baseline found for {branch}.",
                file=sys.stderr,
            )
        return 1

    newest_reference_sha, payload = baseline
    if newest_reference_sha != target_sha:
        if not quiet:
            print(
                f"Artifactory Valgrind baseline for {branch} is {newest_reference_sha}, "
                f"not current head {target_sha}.",
                file=sys.stderr,
            )
        return 1

    output_dir.mkdir(parents=True, exist_ok=True)
    temporary = summary.with_suffix(".xml.tmp")
    temporary.write_bytes(payload)
    temporary.replace(summary)
    print(f"Downloaded Artifactory Valgrind baseline for {branch}@{target_sha}.")
    return 0


def upload_baseline(branch: str, target_sha: str, summary: Path) -> int:
    validate_branch_name(branch)
    validate_sha(target_sha)
    checksum = summary_digest(summary)
    if get_target_branch_head_sha(branch) != target_sha:
        print(f"Skipping outdated Valgrind baseline for {branch}@{target_sha}.")
        return 0

    payload = pack_baseline(target_sha, summary.read_bytes())
    existing = read_remote_baseline(branch)
    if baseline_matches(existing, target_sha, checksum):
        print(f"Artifactory baseline already exists for {branch}@{target_sha}.")
        return 0
    reject_same_sha_conflict(existing, branch, target_sha)

    for attempt in range(3):
        try:
            artifactory_request(branch, data=payload).close()
            published = read_remote_baseline(branch)
            if not baseline_matches(published, target_sha, checksum):
                raise RuntimeError(
                    f"Could not verify Artifactory baseline "
                    f"for {branch}@{target_sha}."
                )
            if get_target_branch_head_sha(branch) != target_sha:
                print(f"Published {branch}@{target_sha}, but the branch advanced.")
                return 0
            print(f"Published Artifactory baseline for {branch}@{target_sha}.")
            return 0
        except urllib.error.URLError as error:
            if attempt == 2 or (
                isinstance(error, urllib.error.HTTPError) and error.code < 500
            ):
                raise
            time.sleep(2**attempt)
    return 1


def publish_missing_baseline(branch: str, target_sha: str) -> None:
    validate_sha(target_sha)
    if get_target_branch_head_sha(branch) != target_sha:
        raise RuntimeError(f"Target branch {branch} advanced past {target_sha}.")
    existing = read_remote_baseline(branch)
    if existing is not None:
        newest_reference_sha, _ = existing
        if newest_reference_sha == target_sha:
            print(f"Artifactory baseline already exists for {branch}@{target_sha}.")
            return

    active_run = find_active_run(branch, target_sha)
    if active_run is not None:
        print(f"Valgrind baseline run {active_run} is already active.")
        return

    subprocess.run(
        [
            "gh", "workflow", "run", WORKFLOW_NAME,
            "--ref", WORKFLOW_REF,
            "-f", "checks=valgrind",
            "-f", f"target_branch_head_sha={target_sha}",
            "-f", f"target_branch_name={branch}",
        ],
        check=True,
    )


def wait_for_baseline(branch: str, target_sha: str, output_dir: Path) -> int:
    poll_seconds = int(os.environ.get("VALGRIND_BASELINE_POLL_SECONDS", "30"))
    timeout_seconds = int(os.environ.get("VALGRIND_BASELINE_TIMEOUT_SECONDS", "5400"))
    deadline = time.monotonic() + timeout_seconds
    attempt = 0

    while True:
        if download_baseline(branch, target_sha, output_dir, quiet=True) == 0:
            return 0
        if get_target_branch_head_sha(branch) != target_sha:
            print(
                f"Target branch {branch} advanced while waiting for "
                f"baseline {target_sha}.",
                file=sys.stderr,
            )
            return 1
        if time.monotonic() >= deadline:
            print(
                f"Timed out waiting for Artifactory Valgrind baseline "
                f"{branch}@{target_sha}.",
                file=sys.stderr,
            )
            return 1
        attempt += 1
        if attempt % 4 == 1:
            print(f"Waiting for Artifactory baseline {branch}@{target_sha}.")
        time.sleep(poll_seconds)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("publish")

    wait = subparsers.add_parser("wait")
    wait.add_argument("--output-dir", required=True, type=Path)

    upload = subparsers.add_parser("upload")
    upload.add_argument("--input", required=True, type=Path)

    args = parser.parse_args()
    try:
        branch = target_branch_name()
        target_sha = required_env("TARGET_BRANCH_HEAD_SHA")
        validate_sha(target_sha)
        if args.command == "publish":
            publish_missing_baseline(branch, target_sha)
            return 0
        if args.command == "wait":
            return wait_for_baseline(branch, target_sha, args.output_dir)
        return upload_baseline(branch, target_sha, args.input)
    except (
        ET.ParseError,
        OSError,
        RuntimeError,
        subprocess.CalledProcessError,
        ValueError,
    ) as error:
        print(f"Artifactory Valgrind baseline unavailable: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
