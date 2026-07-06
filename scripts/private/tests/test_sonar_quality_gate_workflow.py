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


def load_module():
    spec = importlib.util.spec_from_file_location("sonar_quality_gate_workflow_under_test", MODULE_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load {MODULE_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


sonar_quality_gate_workflow = load_module()


class SonarQualityGateWorkflowTests(unittest.TestCase):
    def test_quality_gate_report_command_runs_report_script_directly(self):
        with mock.patch.dict(
            os.environ,
            {
                "SONAR_BRANCH": "feature/test",
                "PR_KEY": "101",
            },
            clear=False,
        ):
            command = sonar_quality_gate_workflow.quality_gate_report_command(probe_api_access=True)

        self.assertEqual(command[0], sonar_quality_gate_workflow.sys.executable)
        self.assertEqual(command[1], "scripts/private/sonar_quality_gate_report.py")
        self.assertNotIn("docker", command)
        self.assertNotIn("pek-sonar-check", command)
        self.assertNotIn("bash", command)
        self.assertIn("scripts/private/sonar_quality_gate_report.py", command)
        self.assertIn(".scannerwork/report-task.txt", command)
        self.assertIn("--branch", command)
        self.assertIn("feature/test", command)
        self.assertIn("--pull-request-key", command)
        self.assertIn("101", command)
        self.assertIn("--probe-api-access", command)

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
