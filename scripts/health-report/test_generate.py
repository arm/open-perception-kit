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
import json
import os
import subprocess
import sys
from pathlib import Path
from tempfile import TemporaryDirectory

import yaml

from generate import apply_coverage_thresholds, build_report, container_sizes, coverage_results, nightly_success_rate

THRESHOLDS = {
    "coverage": {"python": {"minimum_percent": 57}, "native": {"minimum_percent": 75}},
    "deployment_image": {"x86_64": {"maximum_bytes": None}, "aarch64": {"maximum_bytes": None}},
}


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
                Path(temp_dir), THRESHOLDS,
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
                                  releases, logs, None, logs, THRESHOLDS)

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

    def test_thresholds_compare_exact_counts_and_mark_missing_coverage(self):
        coverage = {
            "python": {"status": "available", "covered_lines": 56999, "executable_lines": 100000},
            "native": {"status": "unavailable"},
        }
        apply_coverage_thresholds(coverage, THRESHOLDS)
        self.assertEqual(coverage["python"]["threshold_status"], "failed")
        self.assertEqual(coverage["native"]["threshold_status"], "unavailable")

        coverage["python"]["covered_lines"] = 57000
        coverage["native"] = {"status": "available", "covered_lines": 3, "executable_lines": 4}
        apply_coverage_thresholds(coverage, THRESHOLDS)
        self.assertEqual(coverage["python"]["threshold_status"], "passed")
        self.assertEqual(coverage["native"]["threshold_status"], "passed")

    def test_invalid_threshold_is_rejected(self):
        coverage = {"python": {"status": "unavailable"}, "native": {"status": "unavailable"}}
        with self.assertRaises(ValueError):
            apply_coverage_thresholds(coverage, {**THRESHOLDS, "coverage": {
                "python": {"minimum_percent": True}, "native": {"minimum_percent": 75},
            }})

    def test_container_sizes_check_each_architecture_and_missing_measurements(self):
        thresholds = {**THRESHOLDS, "deployment_image": {
            "x86_64": {"maximum_bytes": 105}, "aarch64": {"maximum_bytes": 105},
        }}
        with TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            deployment = root / "nightly-archive-logs-x86_64"
            deployment.mkdir()
            development = root / "nightly-development-logs-x86_64" / "nightly-logs"
            development.mkdir(parents=True)
            measurement = {"image_bytes": 105, "writable_layer_bytes": 2,
                           "external_model_bytes": 3, "total_bytes": 110}
            (deployment / "container-size.json").write_text(json.dumps(measurement), encoding="utf-8")
            (development / "container-size.json").write_text(json.dumps(measurement), encoding="utf-8")
            results = container_sizes("nightly", root, root, thresholds)
            self.assertEqual(results["development"]["x86_64"]["image_bytes"], 105)
            self.assertEqual(results["deployment"]["x86_64"]["threshold_status"], "passed")
            self.assertEqual(results["deployment"]["aarch64"]["threshold_status"], "unavailable")
            measurement["image_bytes"] = 106
            measurement["total_bytes"] = 111
            (deployment / "container-size.json").write_text(json.dumps(measurement), encoding="utf-8")
            results = container_sizes("nightly", root, root, thresholds)
            self.assertEqual(results["deployment"]["x86_64"]["threshold_status"], "failed")
            self.assertNotIn("threshold_status", results["development"]["x86_64"])

    def test_container_measurement_script_uses_docker_and_model_directories(self):
        with TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            docker = root / "docker"
            docker.write_text(
                '#!/bin/sh\n'
                'case "$1" in\n'
                "  container) echo '[{\"Image\":\"sha256:abc\",\"SizeRw\":7}]' ;;\n"
                "  image) echo '[{\"Size\":100}]' ;;\n"
                'esac\n', encoding="utf-8")
            docker.chmod(0o755)
            model = root / "model"
            model.write_bytes(b"model")
            env = dict(os.environ, PATH=f"{root}:{os.environ['PATH']}")
            output = subprocess.check_output([
                sys.executable, str(Path(__file__).with_name("container-size.py")),
                "test-container", "--model-dir", str(model),
            ], text=True, env=env)
            self.assertEqual(json.loads(output), {"image_bytes": 100, "writable_layer_bytes": 7,
                                                  "external_model_bytes": 5, "total_bytes": 112})

    def test_generator_writes_report_and_failing_output_for_missing_coverage(self):
        with TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            for filename, contents in (("jobs.json", "[]"), ("releases.json", "null"),
                                       ("nightlies.json", "null")):
                (root / filename).write_text(contents, encoding="utf-8")
            for directory in (root / "logs/nightly-development-logs-x86_64/nightly-logs",
                              root / "deployment/nightly-archive-logs-x86_64"):
                directory.mkdir(parents=True)
                (directory / "container-size.json").write_text("", encoding="utf-8")
            (root / "thresholds.yml").write_text(yaml.safe_dump(THRESHOLDS), encoding="utf-8")
            env = dict(os.environ, GITHUB_RUN_ID="1", GITHUB_RUN_ATTEMPT="1",
                       GITHUB_SERVER_URL="https://example.test", GITHUB_REPOSITORY="test/repo",
                       HEALTH_REPORT_NEEDS="{}", GITHUB_STEP_SUMMARY=str(root / "summary"),
                       GITHUB_OUTPUT=str(root / "output"))
            subprocess.run([
                sys.executable, str(Path(__file__).with_name("generate.py")), "--kind", "nightly",
                "--source-commit", "abc", "--jobs", str(root / "jobs.json"),
                "--logs-dir", str(root / "logs"), "--valgrind-dir", str(root / "valgrind"),
                "--deployment-logs-dir", str(root / "deployment"),
                "--coverage-dir", str(root / "coverage"), "--releases", str(root / "releases.json"),
                "--nightlies", str(root / "nightlies.json"), "--thresholds", str(root / "thresholds.yml"),
                "--output", str(root / "report.yml"),
            ], env=env, check=True)
            report = yaml.safe_load((root / "report.yml").read_text(encoding="utf-8"))
            self.assertEqual(report["coverage"]["python"]["threshold_status"], "unavailable")
            self.assertEqual(report["container_sizes"]["development"]["x86_64"]["status"], "unavailable")
            self.assertEqual(report["container_sizes"]["deployment"]["x86_64"]["threshold_status"], "unavailable")
            self.assertEqual((root / "output").read_text(encoding="utf-8"),
                             "coverage_passed=false\ncontainer_sizes_passed=false\n")


if __name__ == "__main__":
    unittest.main()
