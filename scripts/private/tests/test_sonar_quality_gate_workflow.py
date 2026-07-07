#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock


MODULE_PATH = Path(__file__).resolve().parents[1] / "sonar_quality_gate_workflow.py"
REPO_ROOT = MODULE_PATH.parents[2]
REPORT_MODULE_PATH = MODULE_PATH.parent / "sonar_quality_gate_report.py"


def load_module():
    spec = importlib.util.spec_from_file_location("sonar_quality_gate_workflow_under_test", MODULE_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load {MODULE_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_report_module():
    spec = importlib.util.spec_from_file_location("sonar_quality_gate_report_under_test", REPORT_MODULE_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load {REPORT_MODULE_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


sonar_quality_gate_workflow = load_module()
sonar_quality_gate_report = load_report_module()


class SonarQualityGateWorkflowTests(unittest.TestCase):
    def test_quality_gate_report_command_runs_report_script_in_sonar_service(self):
        with mock.patch.dict(
            os.environ,
            {
                "DOCKER_COMPOSE_FILE": ".github/compose.ci.yaml",
                "SONAR_BRANCH": "feature/test",
                "PR_KEY": "101",
                "PR_BRANCH": "feature/test",
                "PR_BASE": "main",
            },
            clear=False,
        ):
            command = sonar_quality_gate_workflow.quality_gate_report_command(probe_api_access=True)

        self.assertEqual(command[:4], ["docker", "compose", "-f", ".github/compose.ci.yaml"])
        self.assertIn("--entrypoint", command)
        self.assertEqual(command[command.index("--entrypoint") + 1], "python3")
        for env_name in ["SONAR_TOKEN", "SONAR_HOST_URL", "SONAR_BRANCH", "PR_KEY", "PR_BRANCH", "PR_BASE"]:
            self.assertIn(env_name, command)
        service_index = command.index("pek-sonar-check")
        self.assertEqual(command[service_index + 1], "scripts/private/sonar_quality_gate_report.py")
        self.assertEqual(command[command.index("--report-task-file") + 1], "/work/.scannerwork/report-task.txt")
        self.assertNotIn("bash", command)
        self.assertIn("scripts/private/sonar_quality_gate_report.py", command)
        self.assertEqual(command[command.index("--branch") + 1], "feature/test")
        self.assertEqual(command[command.index("--pull-request-key") + 1], "101")
        self.assertEqual(command[command.index("--pull-request-branch") + 1], "feature/test")
        self.assertEqual(command[command.index("--pull-request-base") + 1], "main")
        self.assertIn("--probe-api-access", command)

    def test_container_report_task_path_matches_compose_work_bind_mount(self):
        compose_base = (REPO_ROOT / "compose.base.yaml").read_text(encoding="utf-8")

        command = sonar_quality_gate_workflow.quality_gate_report_command(probe_api_access=False)

        self.assertIn("- .:/work", compose_base)
        self.assertEqual(command[command.index("--report-task-file") + 1], "/work/.scannerwork/report-task.txt")

    def test_report_task_context_preserves_pull_request_scope(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            report_task = Path(temp_dir) / "report-task.txt"
            report_task.write_text(
                "\n".join(
                    [
                        "serverUrl=https://sonar.example.invalid",
                        "ceTaskId=task-1",
                        "projectKey=amp-dev-forge",
                    ]
                )
                + "\n",
                encoding="utf-8",
            )

            context = sonar_quality_gate_report.load_report_task(
                report_task,
                "feature/test",
                "101",
                "feature/test",
                "main",
            )

        self.assertEqual(context["pullRequest"], "101")
        self.assertEqual(context["pullRequestBranch"], "feature/test")
        self.assertEqual(context["pullRequestBase"], "main")

    def test_append_summary_writes_report_tail(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            summary_path = Path(temp_dir) / "summary.md"
            report_path = Path(temp_dir) / "report.txt"
            report_path.write_text("one\ntwo\nthree\n", encoding="utf-8")

            with mock.patch.dict(os.environ, {"GITHUB_STEP_SUMMARY": str(summary_path)}, clear=False):
                sonar_quality_gate_workflow.append_summary(
                    title="Sonar quality gate report",
                    report_file=report_path,
                    summary_lines=2,
                )

            summary = summary_path.read_text(encoding="utf-8")
            self.assertIn("### Sonar quality gate report", summary)
            self.assertIn("two\nthree", summary)
            self.assertNotIn("one\ntwo\nthree", summary)


if __name__ == "__main__":
    unittest.main()
