#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import argparse
import json
import os
import subprocess
import sys
import time
from typing import Dict, List, Optional


ACTIVE_STATUSES = {"queued", "in_progress", "requested", "waiting", "pending"}
BASELINE_RUN_EVENTS = {"push", "workflow_dispatch"}


def gh_json(*args: str):
    result = subprocess.run(
        ["gh", *args],
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(result.stdout)


def gh_text(*args: str) -> str:
    result = subprocess.run(
        ["gh", *args],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def baseline_branch() -> str:
    return os.environ.get("VALGRIND_BASELINE_BRANCH", "develop")


def workflow_name() -> str:
    return os.environ.get("VALGRIND_BASELINE_WORKFLOW", "valgrind.yml")


def artifact_name() -> str:
    return os.environ.get("VALGRIND_BASELINE_ARTIFACT", "valgrind-baseline")


def repository() -> str:
    return os.environ["GITHUB_REPOSITORY"]


def current_branch_sha(branch: str) -> str:
    return gh_text(
        "api",
        f"repos/{repository()}/git/ref/heads/{branch}",
        "--jq",
        ".object.sha",
    )


def list_runs(
    branch: str,
    baseline_sha: str,
    status: Optional[str] = None,
) -> List[Dict[str, object]]:
    args = [
        "run",
        "list",
        "--repo",
        repository(),
        "--workflow",
        workflow_name(),
        "--branch",
        branch,
        "--commit",
        baseline_sha,
        "--limit",
        "20",
        "--json",
        "databaseId,event,status",
    ]
    if status:
        args.extend(["--status", status])
    return gh_json(*args)


def run_has_artifact(run_id: int) -> bool:
    payload = gh_json(
        "api",
        f"repos/{repository()}/actions/runs/{run_id}/artifacts",
    )
    return any(
        artifact.get("name") == artifact_name() and not artifact.get("expired", False)
        for artifact in payload.get("artifacts", [])
    )


def find_artifact_run(branch: str, baseline_sha: str) -> Optional[int]:
    for run in list_runs(branch, baseline_sha, status="success"):
        if run.get("event") not in BASELINE_RUN_EVENTS:
            continue
        run_id = int(run["databaseId"])
        if run_has_artifact(run_id):
            return run_id
    return None


def find_active_run(branch: str, baseline_sha: str) -> Optional[int]:
    current_run_id = os.environ.get("GITHUB_RUN_ID")
    for run in list_runs(branch, baseline_sha):
        run_id = int(run["databaseId"])
        if current_run_id and str(run_id) == current_run_id:
            continue
        if run.get("event") not in BASELINE_RUN_EVENTS:
            continue
        if run.get("status") in ACTIVE_STATUSES:
            return run_id
    return None


def write_github_output(run_id: int) -> None:
    output_path = os.environ.get("GITHUB_OUTPUT")
    if output_path:
        with open(output_path, "a", encoding="utf-8") as output:
            output.write(f"run-id={run_id}\n")


def publish_missing_baseline() -> int:
    branch = baseline_branch()
    baseline_sha = current_branch_sha(branch)

    artifact_run = find_artifact_run(branch, baseline_sha)
    if artifact_run is not None:
        print(f"Baseline artifact already exists in run {artifact_run}.")
        return 0

    active_run = find_active_run(branch, baseline_sha)
    if active_run is not None:
        print(f"Baseline run {active_run} is already active.")
        return 0

    subprocess.run(
        ["gh", "workflow", "run", workflow_name(), "--ref", branch],
        check=True,
    )
    return 0


def wait_for_baseline() -> int:
    branch = baseline_branch()
    baseline_sha = current_branch_sha(branch)
    poll_seconds = int(os.environ.get("VALGRIND_BASELINE_POLL_SECONDS", "30"))
    timeout_seconds = int(os.environ.get("VALGRIND_BASELINE_TIMEOUT_SECONDS", "5400"))

    deadline = time.monotonic() + timeout_seconds
    attempt = 0

    while True:
        artifact_run = find_artifact_run(branch, baseline_sha)
        if artifact_run is not None:
            print(
                f"Using {artifact_name()} artifact from run {artifact_run} "
                f"at {branch} {baseline_sha}."
            )
            write_github_output(artifact_run)
            return 0

        if time.monotonic() >= deadline:
            print(f"Timed out waiting for {artifact_name()} on {branch}.", file=sys.stderr)
            return 1

        attempt += 1
        if attempt % 4 == 1:
            print(f"Waiting for {artifact_name()} artifact on {branch} {baseline_sha}.")
        time.sleep(poll_seconds)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=["publish", "wait"])
    args = parser.parse_args()

    if args.command == "publish":
        return publish_missing_baseline()
    return wait_for_baseline()


if __name__ == "__main__":
    raise SystemExit(main())
