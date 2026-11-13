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

import unittest
import subprocess
from tempfile import TemporaryDirectory

from generate import test_results
from pathlib import Path

import yaml


ROOT = Path(__file__).resolve().parents[2]


class HealthReportWorkflowTest(unittest.TestCase):
    def test_report_is_uploaded_before_enforcing_thresholds(self):
        workflow = yaml.safe_load((ROOT / ".github/workflows/health-report.yml").read_text())
        steps = workflow["jobs"]["report"]["steps"]
        names = [step.get("name") for step in steps]
        self.assertLess(names.index("Upload health report"), names.index("Check coverage thresholds"))
        self.assertLess(names.index("Link health report"), names.index("Check coverage thresholds"))

    def test_report_is_uploaded_without_zip(self):
        workflow = yaml.safe_load((ROOT / ".github/workflows/health-report.yml").read_text())
        job = workflow["jobs"]["report"]
        upload = next(step for step in job["steps"] if step.get("name") == "Upload health report")
        self.assertFalse(upload["with"]["archive"])
        self.assertEqual(upload["with"]["path"], "${{ env.REPORT_FILE }}")
        self.assertIn("github.run_attempt", job["env"]["REPORT_FILE"])

    def test_development_collects_each_configuration_once(self):
        workflow = yaml.safe_load((ROOT / ".github/workflows/nightly-development.yml").read_text())
        steps = workflow["jobs"]["development"]["steps"]
        upload = next(step for step in steps if step.get("name") == "Retain logs")
        self.assertEqual(upload["with"]["path"], "nightly-logs/")
        with TemporaryDirectory() as directory:
            root = Path(directory)
            logs = root / "development/build/meson-logs"
            logs.mkdir(parents=True)
            (root / "development/build-active").symlink_to("build")
            (root / "nightly-logs").mkdir()
            # Identical counts still represent two distinct configuration runs.
            for configuration in ("debug", "release"):
                (logs / "testlog.junit.xml").write_text('<testsuite tests="3"/>', encoding="utf-8")
                step = next(step for step in steps if step.get("name") == f"Retain {configuration} test logs")
                self.assertIn("always()", step["if"])
                if configuration == "release":
                    self.assertIn("steps.release.outcome != 'skipped'", step["if"])
                subprocess.run(["bash", "-e", "-o", "pipefail", "-c", step["run"]], cwd=root, check=True)
            reports = test_results(root / "nightly-logs")["reports"]
            self.assertEqual([report["source"] for report in reports],
                             ["debug-meson-logs/testlog.junit.xml", "release-meson-logs/testlog.junit.xml"])
            self.assertEqual([report["tests"] for report in reports], [3, 3])

    def test_report_runs_after_all_workflow_jobs_even_on_failure(self):
        for name in ("nightly", "release"):
            with self.subTest(workflow=name):
                workflow = yaml.safe_load((ROOT / f".github/workflows/{name}.yml").read_text())
                jobs = workflow["jobs"]
                report = jobs["health-report"]

                self.assertEqual(report["if"], "always()")
                self.assertEqual(set(report["needs"]), set(jobs) - {"health-report"})
                self.assertEqual(report["uses"], "./.github/workflows/health-report.yml")


if __name__ == "__main__":
    unittest.main()
