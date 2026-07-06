#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys


PROBE_REPORT = "sonar-api-access-probe.txt"
QUALITY_REPORT = "sonar-quality-gate-report.txt"
REPORT_TASK_FILE = ".scannerwork/report-task.txt"


def run_command(args: list[str], *, stdout=None, stderr=None, check: bool = True) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(args, check=check, stdout=stdout, stderr=stderr)


def report_path(name: str) -> Path:
    runner_temp = os.environ.get("RUNNER_TEMP", "")
    if not runner_temp:
        raise RuntimeError("RUNNER_TEMP is required for Sonar workflow reports.")
    return Path(runner_temp) / name


def step_summary_path() -> Path | None:
    value = os.environ.get("GITHUB_STEP_SUMMARY", "")
    return Path(value) if value else None


def quality_gate_report_command(*, probe_api_access: bool) -> list[str]:
    # Sonar analysis runs in pek-sonar-check with the repository bind-mounted as
    # .:/work, so /work/.scannerwork/report-task.txt is this host checkout path.
    # The report helper is stdlib-only; keep it host-side instead of adding a
    # second compose entrypoint for reporting.
    command = [
        sys.executable,
        "scripts/private/sonar_quality_gate_report.py",
        "--report-task-file",
        REPORT_TASK_FILE,
        "--branch",
        os.environ.get("SONAR_BRANCH", ""),
        "--pull-request-key",
        os.environ.get("PR_KEY", ""),
    ]
    if probe_api_access:
        command.append("--probe-api-access")
    return command


def append_summary(*, title: str, report_file: Path, summary_lines: int) -> None:
    summary_path = step_summary_path()
    if summary_path is None:
        return
    lines = report_file.read_text(encoding="utf-8", errors="replace").splitlines()
    tail = lines[-summary_lines:]
    with summary_path.open("a", encoding="utf-8") as summary:
        summary.write(f"### {title}\n\n")
        summary.write("Full report is available in the `sonar-quality-gate-report` artifact.\n\n")
        summary.write("```text\n")
        summary.write("\n".join(tail))
        if tail:
            summary.write("\n")
        summary.write("```\n")


def run_report(*, report_file: Path, title: str, summary_lines: int, probe_api_access: bool) -> int:
    report_file.parent.mkdir(parents=True, exist_ok=True)
    if probe_api_access:
        report_file.write_text("=== Sonar API access probe ===\n", encoding="utf-8")
        mode = "ab"
    else:
        mode = "wb"

    with report_file.open(mode) as output:
        completed = run_command(
            quality_gate_report_command(probe_api_access=probe_api_access),
            stdout=output,
            stderr=subprocess.STDOUT,
            check=False,
        )

    sys.stdout.write(report_file.read_text(encoding="utf-8", errors="replace"))
    append_summary(title=title, report_file=report_file, summary_lines=summary_lines)
    return int(completed.returncode)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run Sonar quality-gate workflow report helpers.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    probe = subparsers.add_parser("probe-api-access")
    probe.add_argument("--summary-lines", type=int, default=80)

    report = subparsers.add_parser("report-quality-gate")
    report.add_argument("--summary-lines", type=int, default=160)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.command == "probe-api-access":
        return run_report(
            report_file=report_path(PROBE_REPORT),
            title="Sonar API access probe",
            summary_lines=args.summary_lines,
            probe_api_access=True,
        )
    if args.command == "report-quality-gate":
        return run_report(
            report_file=report_path(QUALITY_REPORT),
            title="Sonar quality gate report",
            summary_lines=args.summary_lines,
            probe_api_access=False,
        )
    raise RuntimeError(f"Unsupported command: {args.command}")


if __name__ == "__main__":
    raise SystemExit(main())
