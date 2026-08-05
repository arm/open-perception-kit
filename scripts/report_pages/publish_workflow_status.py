#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
from __future__ import annotations

import json
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path
from urllib import parse

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(REPO_ROOT))

from scripts.report_pages.publish import (  # noqa: E402
    PublishError,
    WORKFLOW_PR_REPORTS,
    WORKFLOW_STATUS_DIRECTORY,
    checkout_site_branch,
    env,
    push_site_branch,
    remove_legacy_root_site,
    require_env,
    set_output,
    write_root_index,
)
from scripts.playwright.pages.publish_playwright_pages import (  # noqa: E402
    write_site_index as write_playwright_index,
)


DRY_RUN_ENV = "REPORT_STATUS_PAGES_DRY_RUN"
STORAGE_BRANCH = "playwright-pages"
WORKFLOW_SOURCES = {
    ".github/workflows/pek-ci.yml": ("pek-ci", "Perception Experience Kit CI Pipeline"),
    ".github/workflows/python-dependency-audit.yml": ("python-audit", "Python Dependency Audit"),
    ".github/workflows/docker-scout-image-audit.yml": ("docker-scout", "Docker Scout Image Audit"),
    ".github/workflows/workflow-audit.yml": ("workflow-freshness", "Workflow Dependency Freshness"),
    ".github/workflows/yolo-benchmark.yml": ("yolo-video", "YOLO Video Benchmark"),
    ".github/workflows/yolo-imageset-benchmark.yml": ("yolo-imageset", "YOLO Imageset Benchmark"),
    ".github/workflows/valgrind.yml": ("valgrind", "Valgrind Baseline Artifact"),
}
WORKFLOW_NAMES = {name: (source, name) for source, name in WORKFLOW_SOURCES.values()}
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


def workflow_jobs(repository: str, run_id: str) -> list[dict[str, object]]:
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
        return []
    return [job for job in jobs if isinstance(job, dict)]


def job_summary(jobs: list[dict[str, object]], conclusion: str) -> list[str]:
    if conclusion == "success":
        return []

    target_conclusions = {"cancelled"} if conclusion == "cancelled" else FAILURE_CONCLUSIONS
    messages = []
    for job in jobs:
        if job.get("conclusion") not in target_conclusions:
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


def freshness_metric(repository: str, run_id: str) -> tuple[str, str]:
    with tempfile.TemporaryDirectory() as tmpdir:
        result = subprocess.run(
            ["gh", "run", "download", run_id, "--repo", repository,
             "--name", "workflow-dependency-freshness", "--dir", tmpdir],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        paths = list(Path(tmpdir).rglob("workflow-dependency-freshness.json"))
        if result.returncode != 0 or len(paths) != 1:
            return "", ""
        try:
            report = json.loads(paths[0].read_text(encoding="utf-8"))
            behind = int(report["behind_latest"])
            review = int(report["needs_review"])
        except (KeyError, TypeError, ValueError, json.JSONDecodeError, OSError):
            return "", ""
    if behind:
        return f"{behind} behind", "neutral"
    if review:
        return f"{review} need{'s' if review == 1 else ''} review", "neutral"
    return "Up to date", "fast"


def docker_scout_report_metric(paths: list[Path], expected_reports: int) -> tuple[str, str]:
    severities = ("critical", "high", "medium", "low", "unspecified")
    totals = dict.fromkeys(severities, 0)
    if len(paths) != expected_reports:
        return "", ""
    for path in paths:
        try:
            report = json.loads(path.read_text(encoding="utf-8"))
            counts = report["severity_counts"]
            if report["sarif_present"] is not True or not isinstance(counts, dict):
                return "", ""
            for severity in severities:
                count = counts[severity]
                if not isinstance(count, int) or isinstance(count, bool) or count < 0:
                    return "", ""
                totals[severity] += count
        except (KeyError, TypeError, json.JSONDecodeError, OSError):
            return "", ""

    visible = [(severity, totals[severity]) for severity in severities if totals[severity]][:2]
    if not visible:
        return "No vulnerabilities", "fast"
    metric = " · ".join(f"{count} {severity}" for severity, count in visible)
    return metric, "slow" if totals["critical"] or totals["high"] else "neutral"


def docker_scout_metric(repository: str, run_id: str,
                        expected_reports: int) -> tuple[str, str]:
    with tempfile.TemporaryDirectory() as tmpdir:
        result = subprocess.run(
            ["gh", "run", "download", run_id, "--repo", repository,
             "--pattern", "docker-scout-*", "--dir", tmpdir],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        paths = list(Path(tmpdir).rglob("component-report.json"))
        if result.returncode != 0:
            return "", ""
        return docker_scout_report_metric(paths, expected_reports)


def valgrind_report_metric(path: Path) -> tuple[str, str]:
    root = ET.parse(path).getroot()
    if root.tag != "valgrindoutput":
        raise ValueError(f"unexpected Valgrind report root: {root.tag}")
    count = int(root.attrib["repo_owned_errors"])
    if count < 0:
        raise ValueError("repo_owned_errors must not be negative")
    return f"{count} repo-owned baseline", "neutral" if count else "fast"


def valgrind_metric(repository: str, run_id: str) -> tuple[str, str]:
    with tempfile.TemporaryDirectory() as tmpdir:
        result = subprocess.run(
            ["gh", "run", "download", run_id, "--repo", repository,
             "--name", "valgrind-baseline", "--dir", tmpdir],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        paths = list(Path(tmpdir).rglob("valgrind-error-summary.xml"))
        if result.returncode != 0 or len(paths) != 1:
            return "", ""
        try:
            return valgrind_report_metric(paths[0])
        except (ET.ParseError, KeyError, OSError, ValueError):
            return "", ""


def workflow_metric(source: str, event: str, conclusion: str,
                    jobs: list[dict[str, object]], repository: str,
                    run_id: str) -> tuple[str, str]:
    if source in {"python-audit", "docker-scout"}:
        if conclusion == "cancelled":
            return "", ""
        active = [job for job in jobs if job.get("conclusion") != "skipped"]
        failed = sum(job.get("conclusion") in FAILURE_CONCLUSIONS for job in active)
        if active:
            if source == "docker-scout":
                metric = docker_scout_metric(repository, run_id, len(active))
                if metric[0]:
                    return metric
                return (f"{failed}/{len(active)} incomplete" if failed
                        else f"{len(active)}/{len(active)} audited"), ""
            return (f"{failed}/{len(active)} failed" if failed else f"{len(active)}/{len(active)} clean"), ""
    if source == "workflow-freshness":
        return freshness_metric(repository, run_id)
    if source == "valgrind" and event == "pull_request" and conclusion == "success":
        return "0 new errors", "fast"
    return "", ""


def workflow_identity(workflow_path: str, workflow_name: str) -> tuple[str, str]:
    identity = WORKFLOW_SOURCES.get(workflow_path) or WORKFLOW_NAMES.get(workflow_name)
    if identity is None:
        raise PublishError(f"Unsupported upstream workflow: {workflow_path or workflow_name}")
    return identity


def status_from_run(repository: str, run: dict[str, object]) -> tuple[str, dict[str, object]] | None:
    event = str(run.get("event", ""))
    branch = str(run.get("head_branch", ""))
    head_repository_value = run.get("head_repository")
    head_repository = (
        str(head_repository_value.get("full_name", ""))
        if isinstance(head_repository_value, dict) else str(head_repository_value or "")
    )
    source, workflow_name = workflow_identity(
        str(run.get("path", "")), str(run.get("name", ""))
    )
    pull_requests = run.get("pull_requests")
    first_pull_request = (
        pull_requests[0]
        if isinstance(pull_requests, list) and pull_requests and isinstance(pull_requests[0], dict)
        else {}
    )
    pull_request_number = str(first_pull_request.get("number", ""))
    scheduled = event == "schedule" and branch == "develop"
    pull_request = event == "pull_request" and source in WORKFLOW_PR_REPORTS
    develop_push = event == "push" and branch == "develop" and source == "valgrind"
    if head_repository != repository or not (scheduled or pull_request or develop_push):
        print(f"Ignoring upstream run: event={event}, branch={branch}, repo={head_repository}")
        return None
    if pull_request and not pull_request_number.isdigit():
        raise PublishError("UPSTREAM_PULL_REQUEST_NUMBER must be numeric for pull requests.")

    run_id = str(run.get("id", ""))
    run_attempt = str(run.get("run_attempt", ""))
    head_sha = str(run.get("head_sha", ""))
    if not run_id.isdigit() or not run_attempt.isdigit():
        raise PublishError("UPSTREAM_RUN_ID and UPSTREAM_RUN_ATTEMPT must be numeric.")
    if not re.fullmatch(r"[0-9a-f]{40}", head_sha):
        raise PublishError("UPSTREAM_HEAD_SHA must be a full lowercase Git SHA.")

    conclusion = str(run.get("conclusion", ""))
    if pull_request and conclusion == "skipped":
        print(f"Ignoring skipped pull request run: workflow={workflow_name}, pr={pull_request_number}")
        return None
    jobs = (
        workflow_jobs(repository, run_id)
        if source in {"python-audit", "docker-scout"} or conclusion != "success" else []
    )
    if source == "valgrind" and event in {"push", "schedule"} and conclusion == "success":
        metric, metric_tone = valgrind_metric(repository, run_id)
    else:
        metric, metric_tone = workflow_metric(source, event, conclusion, jobs, repository, run_id)
    return source, {
        "conclusion": conclusion,
        "event": event,
        "head_branch": branch,
        "head_sha": head_sha,
        "metric": metric,
        "metric_tone": metric_tone,
        "pull_request_number": pull_request_number,
        "repository": repository,
        "run_attempt": run_attempt,
        "run_id": run_id,
        "updated_at": str(run.get("updated_at", "")),
        "workflow": workflow_name,
        "summary": job_summary(jobs, conclusion),
    }


def upstream_status() -> tuple[str, dict[str, object]] | None:
    repository = require_env("GITHUB_REPOSITORY")
    pull_request_number = env("UPSTREAM_PULL_REQUEST_NUMBER")
    return status_from_run(repository, {
        "conclusion": require_env("UPSTREAM_CONCLUSION"),
        "event": require_env("UPSTREAM_EVENT"),
        "head_branch": require_env("UPSTREAM_HEAD_BRANCH"),
        "head_repository": require_env("UPSTREAM_HEAD_REPOSITORY"),
        "head_sha": require_env("UPSTREAM_HEAD_SHA"),
        "id": require_env("UPSTREAM_RUN_ID"),
        "name": env("UPSTREAM_WORKFLOW_NAME"),
        "path": env("UPSTREAM_WORKFLOW_PATH"),
        "pull_requests": ([{"number": pull_request_number}] if pull_request_number else []),
        "run_attempt": require_env("UPSTREAM_RUN_ATTEMPT"),
        "updated_at": require_env("UPSTREAM_UPDATED_AT"),
    })


def latest_scheduled_statuses(repository: str) -> list[tuple[str, dict[str, object]]]:
    statuses = []
    for workflow_path in WORKFLOW_SOURCES:
        encoded_path = parse.quote(workflow_path, safe="")
        result = subprocess.run(
            ["gh", "api", f"repos/{repository}/actions/workflows/{encoded_path}/runs"
             "?event=schedule&branch=develop&status=completed&per_page=1"],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        try:
            runs = json.loads(result.stdout).get("workflow_runs", []) if result.returncode == 0 else []
            run = runs[0] if runs and isinstance(runs[0], dict) else None
        except (AttributeError, json.JSONDecodeError):
            run = None
        if run is None:
            continue
        run["path"] = workflow_path
        selected = status_from_run(repository, run)
        if selected is not None:
            statuses.append(selected)
    return statuses


def status_path(site_dir: Path, source: str, status: dict[str, object]) -> Path:
    status_dir = site_dir / WORKFLOW_STATUS_DIRECTORY / source
    if status["event"] == "schedule":
        return status_dir / "nightly.json"
    if status["event"] == "push":
        return status_dir / "develop.json"
    return status_dir / "prs" / f'{status["pull_request_number"]}.json'


def publish(site_dir: Path, storage_branch: str = STORAGE_BRANCH) -> bool:
    selected = upstream_status()
    if selected is None:
        set_output("deploy", "false")
        return False
    selections = {selected[0]: selected[1]}
    if selected[1]["event"] == "schedule" and env("REPORT_STATUS_RECONCILE_SCHEDULED") == "1":
        for source, status in latest_scheduled_statuses(str(selected[1]["repository"])):
            if run_order(status) >= run_order(selections.get(source)):
                selections[source] = status

    checkout_site_branch(site_dir, storage_branch, DRY_RUN_ENV, "local-report-status-pages")
    remove_legacy_root_site(site_dir)
    write_playwright_index(site_dir, str(selected[1]["repository"]))
    published = []
    for source, status in selections.items():
        path = status_path(site_dir, source, status)
        path.parent.mkdir(parents=True, exist_ok=True)
        existing = read_status(path)
        if status["event"] == "schedule":
            legacy = read_status(site_dir / WORKFLOW_STATUS_DIRECTORY / f"{source}.json")
            if run_order(legacy) > run_order(existing):
                existing = legacy
        if run_order(existing) > run_order(status):
            print(
                f"Ignoring stale {status['workflow']} run {status['run_id']} "
                f"attempt {status['run_attempt']}."
            )
            continue
        path.write_text(json.dumps(status, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        published.append(str(status["workflow"]))

    if not published:
        set_output("deploy", "false")
        return False

    write_root_index(site_dir)
    changed = push_site_branch(
        site_dir,
        storage_branch,
        DRY_RUN_ENV,
        (f"Update {published[0]} report status" if len(published) == 1
         else "Reconcile nightly report status"),
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
