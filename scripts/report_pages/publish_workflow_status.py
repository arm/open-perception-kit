#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(REPO_ROOT))

from scripts.report_pages.publish import (  # noqa: E402
    PublishError,
    WORKFLOW_STATUS_DIRECTORY,
    checkout_site_branch,
    env,
    push_site_branch,
    require_env,
    set_output,
    write_root_index,
)


DRY_RUN_ENV = "REPORT_STATUS_PAGES_DRY_RUN"
STORAGE_BRANCH = "playwright-pages"
WORKFLOW_SOURCES = {
    "Perception Experience Kit CI Pipeline": "pek-ci",
    "Python Dependency Audit": "python-audit",
    "Docker Scout Image Audit": "docker-scout",
    "Workflow Dependency Freshness": "workflow-freshness",
    "YOLO Video Benchmark": "yolo-video",
    "YOLO Imageset Benchmark": "yolo-imageset",
}
FAILURE_CONCLUSIONS = {"action_required", "failure", "startup_failure", "timed_out"}


def run_order(status: dict[str, object] | None) -> tuple[int, int]:
    if not isinstance(status, dict):
        return 0, 0
    try:
        return int(status["run_id"]), int(status.get("run_attempt", 1))
    except (KeyError, TypeError, ValueError):
        return 0, 0


def read_status(path: Path) -> dict[str, object] | None:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
        return value if isinstance(value, dict) else None
    except (OSError, json.JSONDecodeError):
        return None


def job_summary(repository: str, run_id: str, conclusion: str) -> list[str]:
    if conclusion == "success":
        return []
    result = subprocess.run(
        ["gh", "api", f"repos/{repository}/actions/runs/{run_id}/jobs?per_page=100"],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    try:
        jobs = json.loads(result.stdout).get("jobs", []) if result.returncode == 0 else []
    except (AttributeError, json.JSONDecodeError):
        jobs = []

    target_conclusions = {"cancelled"} if conclusion == "cancelled" else FAILURE_CONCLUSIONS
    messages = []
    for job in jobs:
        if not isinstance(job, dict) or job.get("conclusion") not in target_conclusions:
            continue
        name = job.get("name")
        if not isinstance(name, str) or not name:
            continue
        steps = job.get("steps")
        failed_steps = [
            step.get("name")
            for step in (steps if isinstance(steps, list) else []) if isinstance(step, dict)
            and step.get("conclusion") in target_conclusions and isinstance(step.get("name"), str)
        ]
        messages.append(f"{name}: {', '.join(failed_steps[:2])}" if failed_steps else name)
        if len(messages) == 3:
            break
    if messages:
        return messages
    if conclusion == "cancelled":
        return ["Run cancelled before all jobs completed."]
    return [f"Run {conclusion.replace('_', ' ')}; open the workflow run for details."]


def upstream_status() -> tuple[str, dict[str, object]] | None:
    event = require_env("UPSTREAM_EVENT")
    branch = require_env("UPSTREAM_HEAD_BRANCH")
    repository = require_env("GITHUB_REPOSITORY")
    head_repository = require_env("UPSTREAM_HEAD_REPOSITORY")
    if event != "schedule" or branch != "develop" or head_repository != repository:
        print(f"Ignoring non-nightly upstream run: event={event}, branch={branch}, repo={head_repository}")
        return None

    workflow_name = require_env("UPSTREAM_WORKFLOW_NAME")
    source = WORKFLOW_SOURCES.get(workflow_name)
    if source is None:
        raise PublishError(f"Unsupported upstream workflow: {workflow_name}")

    run_id = require_env("UPSTREAM_RUN_ID")
    run_attempt = require_env("UPSTREAM_RUN_ATTEMPT")
    head_sha = require_env("UPSTREAM_HEAD_SHA")
    if not run_id.isdigit() or not run_attempt.isdigit():
        raise PublishError("UPSTREAM_RUN_ID and UPSTREAM_RUN_ATTEMPT must be numeric.")
    if not re.fullmatch(r"[0-9a-f]{40}", head_sha):
        raise PublishError("UPSTREAM_HEAD_SHA must be a full lowercase Git SHA.")

    conclusion = require_env("UPSTREAM_CONCLUSION")
    return source, {
        "conclusion": conclusion,
        "head_sha": head_sha,
        "repository": repository,
        "run_attempt": run_attempt,
        "run_id": run_id,
        "updated_at": require_env("UPSTREAM_UPDATED_AT"),
        "workflow": workflow_name,
        "summary": job_summary(repository, run_id, conclusion),
    }


def publish(site_dir: Path, storage_branch: str = STORAGE_BRANCH) -> bool:
    selected = upstream_status()
    if selected is None:
        set_output("deploy", "false")
        return False
    source, status = selected

    checkout_site_branch(site_dir, storage_branch, DRY_RUN_ENV, "local-report-status-pages")
    status_dir = site_dir / WORKFLOW_STATUS_DIRECTORY
    status_dir.mkdir(parents=True, exist_ok=True)
    status_path = status_dir / f"{source}.json"
    if run_order(read_status(status_path)) > run_order(status):
        print(f"Ignoring stale {status['workflow']} run {status['run_id']} attempt {status['run_attempt']}.")
        set_output("deploy", "false")
        return False

    status_path.write_text(json.dumps(status, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_root_index(site_dir)
    changed = push_site_branch(
        site_dir,
        storage_branch,
        DRY_RUN_ENV,
        f"Update {status['workflow']} report status",
        "report status Pages",
    )
    set_output("deploy", "true" if changed else "false")
    return changed


def main(argv: list[str]) -> int:
    if len(argv) > 2:
        print("Usage: publish_workflow_status.py [site-dir]", file=sys.stderr)
        return 2
    publish(Path(argv[1]) if len(argv) == 2 else Path("_report_status_pages_site"))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv))
    except PublishError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1) from error
