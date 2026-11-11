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
from tempfile import TemporaryDirectory

from generate import build_report, coverage_results, nightly_success_rate


class GenerateReportTest(unittest.TestCase):
    def test_report_retains_only_unsuccessful_jobs_and_steps(self):
        pages = [{"jobs": [
            {"name": "report", "status": "in_progress"},
            {
                "name": "build (arm64)", "status": "completed",
                "conclusion": "failure", "html_url": "https://example.test/job",
                "steps": [{"name": "setup", "conclusion": "success"},
                          {"name": "test", "conclusion": "failure"},
                          {"name": "cleanup", "conclusion": "skipped"}],
            },
            {
                "name": "build (amd64)", "status": "completed",
                "conclusion": "success", "html_url": "https://example.test/other",
                "steps": [{"name": "test", "conclusion": "skipped"}],
            },
            {"name": "skipped", "status": "completed", "conclusion": "skipped",
             "html_url": "https://example.test/skipped"},
            {"name": "cancelled", "status": "completed", "conclusion": "cancelled",
             "html_url": "https://example.test/cancelled"},
        ]}]
        with TemporaryDirectory() as temp_dir:
            report = build_report(
                "nightly", "abc", "123", "1", "https://example.test/run",
                {"build": {"result": "failure"}, "docs": {"result": "success"}},
                pages, Path(temp_dir), None, Path(temp_dir), None,
                Path(temp_dir),
            )

        self.assertEqual(report["workflow_jobs"], {"build": "failure"})
        self.assertEqual([job["name"] for job in report["jobs"]],
                         ["build (arm64)", "cancelled"])
        self.assertEqual(report["jobs"][0]["url"], "https://example.test/job")
        self.assertEqual(report["jobs"][0]["steps"], [{"name": "test", "conclusion": "failure"}])
        self.assertEqual(report["jobs"][1]["steps"], [])
        self.assertEqual(report["test_results"]["status"], "unavailable")
        self.assertEqual(report["release_downloads"]["status"], "unavailable")
        self.assertEqual(report["valgrind"]["status"], "unavailable")

    def test_junit_results_are_counted(self):
        with TemporaryDirectory() as temp_dir:
            logs = Path(temp_dir)
            (logs / "testlog.junit.xml").write_text(
                '<testsuites><testsuite tests="4" failures="1" errors="0" skipped="1" />'
                '<testsuite tests="2" failures="0" errors="1" skipped="0" /></testsuites>',
                encoding="utf-8",
            )
            releases = [[{"draft": False, "tag_name": "v1.0.0", "assets": [
                {"name": "opk.tar.gz", "download_count": 42},
            ]}, {"draft": True}]]
            (logs / "valgrind-current.xml").write_text(
                "<valgrindoutput><error/><error/></valgrindoutput>", encoding="utf-8",
            )
            report = build_report("release", "abc", "123", "1", "url", {}, [], logs,
                                  releases, logs, None, logs)

        self.assertEqual(report["test_results"]["reports"][0], {
            "source": "testlog.junit.xml", "tests": 6,
            "failures": 1, "errors": 1, "skipped": 1,
        })
        self.assertEqual(report["release_downloads"]["releases"], [
            {"tag": "v1.0.0", "assets": [{"name": "opk.tar.gz", "downloads": 42}]},
        ])
        self.assertEqual(report["valgrind"]["unique_errors"], 2)

    def test_nightly_rate_excludes_cancelled_and_limits_window(self):
        runs = [{"conclusion": result} for result in
                ["success", "failure", "cancelled"] + ["success"] * 28]
        rate = nightly_success_rate({"workflow_runs": runs})

        self.assertEqual(rate["runs_considered"], 30)
        self.assertEqual(rate["cancelled"], 1)
        self.assertEqual(rate["passed"], 28)
        self.assertEqual(rate["success_percent"], 96.55)

    def test_coverage_reports_are_read_separately(self):
        with TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            python = root / "health-python-coverage"
            native = root / "health-native-coverage"
            python.mkdir()
            native.mkdir()
            (python / "python-coverage.json").write_text(
                '{"totals":{"covered_lines":3,"num_statements":4,"percent_covered":75},'
                '"files":{"scripts/example.py":{}}}', encoding="utf-8",
            )
            (native / "coverage.xml").write_text(
                '<coverage lines-covered="5" lines-valid="10"/>', encoding="utf-8",
            )
            result = coverage_results(root)

        self.assertEqual(result["python"]["line_percent"], 75)
        self.assertEqual(result["python"]["files"], ["scripts/example.py"])
        self.assertEqual(result["native"]["line_percent"], 50)


if __name__ == "__main__":
    unittest.main()
