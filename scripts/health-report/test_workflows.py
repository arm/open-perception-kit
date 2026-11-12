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
