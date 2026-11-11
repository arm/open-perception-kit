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

"""Write a health report from GitHub Actions job results."""

import argparse
import json
import os
from datetime import datetime, timezone
from pathlib import Path
from xml.etree import ElementTree

import yaml

UNSUCCESSFUL_CONCLUSIONS = frozenset({
    "failure", "timed_out", "cancelled", "action_required", "startup_failure", "stale",
})


def test_results(logs_dir):
    reports = []
    for path in sorted(logs_dir.rglob("testlog.junit.xml")):
        root = ElementTree.parse(path).getroot()
        suites = root.findall("testsuite") if root.tag == "testsuites" else [root]
        counts = {name: sum(int(suite.get(name, "0")) for suite in suites)
                  for name in ("tests", "failures", "errors", "skipped")}
        reports.append({"source": str(path.relative_to(logs_dir)), **counts})

    return {"status": "available" if reports else "unavailable", "reports": reports}


def release_downloads(pages):
    if pages is None:
        return {"status": "unavailable", "reason": "GitHub releases could not be fetched"}

    releases = []
    for page in pages:
        for release in page:
            if release["draft"]:
                continue

            releases.append({
                "tag": release["tag_name"],
                "assets": [{"name": asset["name"], "downloads": asset["download_count"]}
                           for asset in release["assets"]],
            })

    return {"status": "available", "releases": releases}


def valgrind_results(logs_dir):
    summaries = sorted(logs_dir.rglob("valgrind-current.xml"))
    if not summaries:
        return {"status": "unavailable"}

    if len(summaries) != 1:
        raise ValueError("Expected one Valgrind summary")

    root = ElementTree.parse(summaries[0]).getroot()
    if root.tag != "valgrindoutput":
        raise ValueError("Invalid Valgrind summary")

    return {"status": "available", "unique_errors": len(root.findall("error"))}


def nightly_success_rate(response):
    if response is None:
        return {"status": "unavailable", "reason": "Nightly runs could not be fetched"}

    runs = response["workflow_runs"][:30]
    outcomes = [run["conclusion"] for run in runs]
    passed = outcomes.count("success")
    cancelled = outcomes.count("cancelled")
    failed = sum(outcome in ("failure", "timed_out", "startup_failure", "action_required")
                 for outcome in outcomes)
    other = len(outcomes) - passed - cancelled - failed
    total = passed + failed
    return {
        "status": "available" if total else "unavailable",
        "scope": "last 30 completed scheduled nightly runs on main, excluding the current run",
        "runs_considered": len(outcomes),
        "passed": passed,
        "failed": failed,
        "cancelled": cancelled,
        "other": other,
        "success_percent": round(100 * passed / total, 2) if total else None,
    }


def coverage_results(coverage_dir):
    results = {"python": {"status": "unavailable"}, "native": {"status": "unavailable"}}
    python_path = coverage_dir / "health-python-coverage" / "python-coverage.json"
    if python_path.is_file():
        python = json.loads(python_path.read_text(encoding="utf-8"))
        totals = python["totals"]
        results["python"] = {
            "status": "available",
            "scope": "scripts and tools/opk-ci/opk_ci; test files excluded; see files for measured set",
            "covered_lines": totals["covered_lines"],
            "executable_lines": totals["num_statements"],
            "line_percent": round(totals["percent_covered"], 2),
            "files": sorted(python["files"]),
        }

    native_paths = list((coverage_dir / "health-native-coverage").rglob("coverage.xml"))
    if len(native_paths) > 1:
        raise ValueError("Expected one native coverage report")
    if native_paths:
        root = ElementTree.parse(native_paths[0]).getroot()
        if root.tag != "coverage":
            raise ValueError("Invalid native coverage report")
        covered = int(root.attrib["lines-covered"])
        total = int(root.attrib["lines-valid"])
        results["native"] = {
            "status": "available",
            "scope": "aarch64 debug build with Python Ops enabled; ExecuTorch and MLWL disabled",
            "covered_lines": covered,
            "executable_lines": total,
            "line_percent": round(100 * covered / total, 2) if total else None,
        }

    return results


def build_report(kind, source_commit, run_id, run_attempt, run_url, needs, pages, logs_dir,
                 releases, valgrind_dir, nightlies, coverage_dir):
    jobs = []
    for page in pages:
        for job in page["jobs"]:
            if job["status"] != "completed" or job["conclusion"] not in UNSUCCESSFUL_CONCLUSIONS:
                continue

            jobs.append({
                "name": job["name"],
                "conclusion": job["conclusion"],
                "url": job["html_url"],
                "steps": [
                    {"name": step["name"], "conclusion": step["conclusion"]}
                    for step in job.get("steps", [])
                    if step["conclusion"] in UNSUCCESSFUL_CONCLUSIONS
                ],
            })

    jobs.sort(key=lambda job: job["name"])
    return {
        "schema_version": 1,
        "report": {
            "kind": kind,
            "source_commit": source_commit,
            "run_id": run_id,
            "run_attempt": run_attempt,
            "run_url": run_url,
            "generated_at": datetime.now(timezone.utc).isoformat(),
        },
        "workflow_jobs": {name: value["result"] for name, value in needs.items()
                          if value["result"] in UNSUCCESSFUL_CONCLUSIONS},
        "jobs": jobs,
        "test_results": test_results(logs_dir),
        "valgrind": valgrind_results(valgrind_dir),
        "release_downloads": release_downloads(releases),
        "nightly_success_rate": nightly_success_rate(nightlies),
        "coverage": coverage_results(coverage_dir),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kind", choices=("nightly", "release"), required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--jobs", type=Path, required=True)
    parser.add_argument("--logs-dir", type=Path, required=True)
    parser.add_argument("--releases", type=Path, required=True)
    parser.add_argument("--valgrind-dir", type=Path, required=True)
    parser.add_argument("--nightlies", type=Path, required=True)
    parser.add_argument("--coverage-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    report = build_report(
        args.kind,
        args.source_commit,
        os.environ["GITHUB_RUN_ID"],
        os.environ["GITHUB_RUN_ATTEMPT"],
        f'{os.environ["GITHUB_SERVER_URL"]}/{os.environ["GITHUB_REPOSITORY"]}'
        f'/actions/runs/{os.environ["GITHUB_RUN_ID"]}',
        json.loads(os.environ["HEALTH_REPORT_NEEDS"]),
        json.loads(args.jobs.read_text(encoding="utf-8")),
        args.logs_dir,
        json.loads(args.releases.read_text(encoding="utf-8")),
        args.valgrind_dir,
        json.loads(args.nightlies.read_text(encoding="utf-8")),
        args.coverage_dir,
    )
    args.output.write_text(yaml.safe_dump(report, sort_keys=False), encoding="utf-8")
    with Path(os.environ["GITHUB_STEP_SUMMARY"]).open("a", encoding="utf-8") as summary:
        summary.write("## Health report\n\n")
        for name, result in report["workflow_jobs"].items():
            summary.write(f"- {name}: {result}\n")
        if not report["workflow_jobs"]:
            summary.write("No failed or cancelled workflow jobs.\n")
        summary.write("\n")


if __name__ == "__main__":
    main()
