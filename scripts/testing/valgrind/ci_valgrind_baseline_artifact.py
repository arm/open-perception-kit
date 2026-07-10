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


ACTIVE_STATUSES = {"queued", "in_progress", "requested", "waiting", "pending"}
ARTIFACT_NAME = os.environ.get("VALGRIND_BASELINE_ARTIFACT", "valgrind-baseline")
BASELINE_BRANCH = os.environ.get("VALGRIND_BASELINE_BRANCH", "develop")
BASELINE_RUN_EVENTS = {"push", "workflow_dispatch"}
REPOSITORY = os.environ["GITHUB_REPOSITORY"]
WORKFLOW_NAME = os.environ.get("VALGRIND_BASELINE_WORKFLOW", "valgrind.yml")


def gh(*args: str) -> str:
    result = subprocess.run(
        ["gh", *args],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def gh_json(*args: str):
    return json.loads(gh(*args))


def current_branch_sha() -> str:
    return gh(
        "api",
        f"repos/{REPOSITORY}/git/ref/heads/{BASELINE_BRANCH}",
        "--jq",
        ".object.sha",
    )


def list_runs(baseline_sha: str, status=None):
    args = [
        "run",
        "list",
        "--repo",
        REPOSITORY,
        "--workflow",
        WORKFLOW_NAME,
        "--branch",
        BASELINE_BRANCH,
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
        f"repos/{REPOSITORY}/actions/runs/{run_id}/artifacts",
    )
    return any(
        artifact.get("name") == ARTIFACT_NAME and not artifact.get("expired", False)
        for artifact in payload.get("artifacts", [])
    )


def find_artifact_run(baseline_sha: str):
    for run in list_runs(baseline_sha, status="success"):
        if run.get("event") not in BASELINE_RUN_EVENTS:
            continue
        run_id = int(run["databaseId"])
        if run_has_artifact(run_id):
            return run_id
    return None


def find_active_run(baseline_sha: str):
    for run in list_runs(baseline_sha):
        if run.get("event") not in BASELINE_RUN_EVENTS:
            continue
        if run.get("status") in ACTIVE_STATUSES:
            return int(run["databaseId"])
    return None


def use_artifact(run_id: int, baseline_sha: str) -> int:
    print(f"Using {ARTIFACT_NAME} artifact from run {run_id} at {BASELINE_BRANCH} {baseline_sha}.")
    output_path = os.environ.get("GITHUB_OUTPUT")
    if output_path:
        with open(output_path, "a", encoding="utf-8") as output:
            output.write(f"run-id={run_id}\n")
    return 0


def publish_missing_baseline() -> int:
    baseline_sha = current_branch_sha()
    artifact_run = find_artifact_run(baseline_sha)
    if artifact_run is not None:
        print(f"Baseline artifact already exists in run {artifact_run}.")
        return 0

    active_run = find_active_run(baseline_sha)
    if active_run is not None:
        print(f"Baseline run {active_run} is already active.")
        return 0

    subprocess.run(
        ["gh", "workflow", "run", WORKFLOW_NAME, "--ref", BASELINE_BRANCH],
        check=True,
    )
    return 0


def locate_baseline() -> int:
    baseline_sha = current_branch_sha()
    artifact_run = find_artifact_run(baseline_sha)
    if artifact_run is not None:
        return use_artifact(artifact_run, baseline_sha)

    print(
        f"No available {ARTIFACT_NAME} artifact found on {BASELINE_BRANCH} at {baseline_sha}.",
        file=sys.stderr,
    )
    print(
        f"Publish {WORKFLOW_NAME} on the current {BASELINE_BRANCH} tip to create a new baseline artifact.",
        file=sys.stderr,
    )
    return 1


def wait_for_baseline() -> int:
    baseline_sha = current_branch_sha()
    poll_seconds = int(os.environ.get("VALGRIND_BASELINE_POLL_SECONDS", "30"))
    timeout_seconds = int(os.environ.get("VALGRIND_BASELINE_TIMEOUT_SECONDS", "5400"))

    deadline = time.monotonic() + timeout_seconds
    attempt = 0

    while True:
        artifact_run = find_artifact_run(baseline_sha)
        if artifact_run is not None:
            return use_artifact(artifact_run, baseline_sha)

        if time.monotonic() >= deadline:
            print(f"Timed out waiting for {ARTIFACT_NAME} on {BASELINE_BRANCH}.", file=sys.stderr)
            return 1

        attempt += 1
        if attempt % 4 == 1:
            print(f"Waiting for {ARTIFACT_NAME} artifact on {BASELINE_BRANCH} {baseline_sha}.")
        time.sleep(poll_seconds)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=["locate", "publish", "wait"])
    args = parser.parse_args()

    if args.command == "locate":
        return locate_baseline()
    if args.command == "publish":
        return publish_missing_baseline()
    return wait_for_baseline()


if __name__ == "__main__":
    raise SystemExit(main())
