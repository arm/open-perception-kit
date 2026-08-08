#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
"""Store exact-SHA Valgrind summaries in GHCR."""

import argparse
import hashlib
import json
import os
import re
import runpy
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


ACTIVE_STATUSES = {"queued", "in_progress", "requested", "waiting", "pending"}
BASELINE_BRANCH = os.environ.get("VALGRIND_BASELINE_BRANCH", "develop")
BASELINE_RUN_EVENTS = {"push", "workflow_dispatch"}
OWNER, REPOSITORY_NAME = os.environ["GITHUB_REPOSITORY"].split("/", 1)
REPOSITORY = f"{OWNER}/{REPOSITORY_NAME}"
WORKFLOW_NAME = os.environ.get("VALGRIND_BASELINE_WORKFLOW", "pek-ci.yml")
WORKFLOW_REF = os.environ.get("VALGRIND_BASELINE_WORKFLOW_REF", "develop")
PACKAGE = f"{REPOSITORY_NAME.lower()}-valgrind-baseline"
IMAGE = f"ghcr.io/{OWNER.lower()}/{PACKAGE}"
SUMMARY_NAME = "valgrind-error-summary.xml"


def gh(*args: str) -> str:
    return subprocess.run(
        ["gh", *args],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def gh_json(*args: str):
    return json.loads(gh(*args))


def docker(*args: str, capture_output: bool = False) -> str:
    result = subprocess.run(
        ["docker", *args],
        check=True,
        capture_output=capture_output,
        text=capture_output,
    )
    return result.stdout.strip() if capture_output else ""


def validate_sha(sha: str) -> None:
    if not re.fullmatch(r"[0-9a-f]{40}", sha):
        raise ValueError(f"Invalid Git commit SHA: {sha}")


def current_branch_sha() -> str:
    return gh(
        "api",
        f"repos/{REPOSITORY}/git/ref/heads/{BASELINE_BRANCH}",
        "--jq",
        ".object.sha",
    )


def baseline_sha(explicit_sha: str = "") -> str:
    sha = explicit_sha or os.environ.get("VALGRIND_BASELINE_SHA", "") or current_branch_sha()
    validate_sha(sha)
    return sha


def list_runs(sha: str):
    return gh_json(
        "run",
        "list",
        "--repo",
        REPOSITORY,
        "--workflow",
        WORKFLOW_NAME,
        "--branch",
        BASELINE_BRANCH,
        "--commit",
        sha,
        "--limit",
        "20",
        "--json",
        "databaseId,event,status",
    )


def find_active_run(sha: str):
    for run in list_runs(sha):
        if run.get("event") in BASELINE_RUN_EVENTS and run.get("status") in ACTIVE_STATUSES:
            return int(run["databaseId"])
    return None


def summary_digest(path: Path) -> str:
    compare = runpy.run_path(
        str(Path(__file__).with_name("compare-valgrind-results.py"))
    )
    summary = compare["load_repository_summary"](path)
    payload = json.dumps(sorted(summary.items()), separators=(",", ":"))
    return hashlib.sha256(payload.encode()).hexdigest()


def commit_tags(sha: str) -> list[str]:
    prefix = f"v2-sha-{sha}-"
    try:
        tags = gh(
            "api",
            "--paginate",
            f"/orgs/{OWNER}/packages/container/{PACKAGE}/versions?per_page=100",
            "--jq",
            ".[].metadata.container.tags[]?",
        )
    except subprocess.CalledProcessError:
        return []
    return sorted(tag for tag in tags.splitlines() if tag.startswith(prefix))


def download_baseline(sha: str, output_dir: Path, *, quiet: bool = False) -> int:
    validate_sha(sha)
    tags = commit_tags(sha)
    if len(tags) != 1:
        if not quiet:
            reason = "not found" if not tags else f"ambiguous ({len(tags)} versions)"
            print(f"GHCR Valgrind baseline {reason} for {sha}.", file=sys.stderr)
        return 1

    tag = tags[0]
    expected_digest = tag.removeprefix(f"v2-sha-{sha}-")
    if not re.fullmatch(r"[0-9a-f]{64}", expected_digest):
        print(f"Ignoring malformed GHCR Valgrind baseline tag: {tag}", file=sys.stderr)
        return 1

    image = f"{IMAGE}:{tag}"
    output_dir.mkdir(parents=True, exist_ok=True)
    summary = output_dir / SUMMARY_NAME
    summary.unlink(missing_ok=True)
    docker("pull", image)
    container = docker("create", image, capture_output=True)
    try:
        docker("cp", f"{container}:/{SUMMARY_NAME}", str(summary))
    finally:
        docker("rm", "-f", container)

    if summary_digest(summary) != expected_digest:
        summary.unlink(missing_ok=True)
        print(f"Ignoring corrupt GHCR Valgrind baseline for {sha}.", file=sys.stderr)
        return 1

    print(f"Downloaded GHCR Valgrind baseline for {sha}.")
    return 0


def upload_baseline(sha: str, summary: Path) -> int:
    validate_sha(sha)
    checksum = summary_digest(summary)
    tag = f"v2-sha-{sha}-{checksum}"
    existing_tags = commit_tags(sha)
    if tag in existing_tags:
        print(f"GHCR Valgrind baseline already exists for {sha}.")
        return 0
    if existing_tags:
        raise RuntimeError(f"Conflicting GHCR Valgrind baseline already exists for {sha}.")

    image = f"{IMAGE}:{tag}"
    with tempfile.TemporaryDirectory() as tmpdir:
        context = Path(tmpdir)
        shutil.copy2(summary, context / SUMMARY_NAME)
        (context / "Dockerfile").write_text(
            "FROM scratch\n"
            f'LABEL org.opencontainers.image.source="https://github.com/{REPOSITORY}"\n'
            f"COPY {SUMMARY_NAME} /{SUMMARY_NAME}\n"
            f'CMD ["/{SUMMARY_NAME}"]\n',
            encoding="utf-8",
        )
        docker("build", "--tag", image, str(context))

    for attempt in range(3):
        try:
            docker("push", image)
            print(f"Published GHCR Valgrind baseline for {sha}.")
            return 0
        except subprocess.CalledProcessError:
            if attempt == 2:
                raise
            time.sleep(2**attempt)
    return 1


def publish_missing_baseline() -> int:
    sha = baseline_sha()
    tags = commit_tags(sha)
    if len(tags) == 1:
        print(f"GHCR Valgrind baseline already exists for {sha}.")
        return 0
    if len(tags) > 1:
        raise RuntimeError(f"Ambiguous GHCR Valgrind baseline for {sha}.")

    active_run = find_active_run(sha)
    if active_run is not None:
        print(f"Valgrind baseline run {active_run} is already active.")
        return 0

    subprocess.run(
        [
            "gh", "workflow", "run", WORKFLOW_NAME,
            "--ref", WORKFLOW_REF,
            "-f", "checks=valgrind",
            "-f", f"valgrind_baseline_sha={sha}",
        ],
        check=True,
    )
    return 0


def wait_for_baseline(sha: str, output_dir: Path) -> int:
    poll_seconds = int(os.environ.get("VALGRIND_BASELINE_POLL_SECONDS", "30"))
    timeout_seconds = int(os.environ.get("VALGRIND_BASELINE_TIMEOUT_SECONDS", "5400"))
    deadline = time.monotonic() + timeout_seconds
    attempt = 0

    while True:
        if download_baseline(sha, output_dir, quiet=True) == 0:
            return 0
        if time.monotonic() >= deadline:
            print(f"Timed out waiting for GHCR Valgrind baseline {sha}.", file=sys.stderr)
            return 1
        attempt += 1
        if attempt % 4 == 1:
            print(f"Waiting for GHCR Valgrind baseline {sha}.")
        time.sleep(poll_seconds)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("publish")

    download = subparsers.add_parser("download")
    download.add_argument("--sha", default="")
    download.add_argument("--output-dir", required=True, type=Path)

    wait = subparsers.add_parser("wait")
    wait.add_argument("--sha", default="")
    wait.add_argument("--output-dir", required=True, type=Path)

    upload = subparsers.add_parser("upload")
    upload.add_argument("--sha", required=True)
    upload.add_argument("--input", required=True, type=Path)

    args = parser.parse_args()
    try:
        if args.command == "publish":
            return publish_missing_baseline()
        if args.command == "download":
            return download_baseline(baseline_sha(args.sha), args.output_dir)
        if args.command == "wait":
            return wait_for_baseline(baseline_sha(args.sha), args.output_dir)
        return upload_baseline(args.sha, args.input)
    except (OSError, RuntimeError, subprocess.CalledProcessError, ValueError) as error:
        print(f"GHCR Valgrind baseline unavailable: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
