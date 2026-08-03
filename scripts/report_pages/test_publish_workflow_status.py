################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
import datetime as dt
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from scripts.report_pages import publish as report_pages
from scripts.report_pages import publish_workflow_status as publisher


NOW = dt.datetime(2026, 8, 3, 12, tzinfo=dt.timezone.utc)
SHA = "a" * 40


def status(conclusion: str, updated_at: str = "2026-08-03T10:00:00Z") -> dict[str, str]:
    return {
        "conclusion": conclusion,
        "head_sha": SHA,
        "repository": "Arm-Debug/amp-dev-forge",
        "run_attempt": "1",
        "run_id": "123",
        "updated_at": updated_at,
        "workflow": "Example",
    }


class TestPublishWorkflowStatus(unittest.TestCase):
    def test_root_index_renders_nightly_states_and_links(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir)
            status_dir = site_dir / report_pages.WORKFLOW_STATUS_DIRECTORY
            status_dir.mkdir()
            fixtures = {
                "pek-ci": status("success"),
                "python-audit": status("failure"),
                "docker-scout": status("cancelled"),
                "workflow-freshness": status("success", "2026-08-01T00:00:00Z"),
            }
            fixtures["python-audit"]["summary"] = ["pip-audit <expkits-ci>: Run pip-audit"]
            for source, payload in fixtures.items():
                (status_dir / f"{source}.json").write_text(json.dumps(payload), encoding="utf-8")

            report_pages.write_root_index(site_dir, now=NOW)
            index = (site_dir / "index.html").read_text(encoding="utf-8")

        for label in ("Passed", "Failure", "Cancelled", "Stale", "Unavailable"):
            self.assertIn(f">{label}<", index)
        self.assertIn("2026-08-03 10:00 UTC", index)
        self.assertIn(f"/commit/{SHA}", index)
        self.assertIn("/actions/runs/123", index)
        self.assertIn("/actions/runs/123#artifacts", index)
        self.assertIn("pip-audit &lt;expkits-ci&gt;: Run pip-audit", index)

    def test_publish_persists_only_develop_schedule(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            output = root / "output.txt"
            environment = {
                "GITHUB_OUTPUT": str(output),
                "GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge",
                "REPORT_STATUS_PAGES_DRY_RUN": "1",
                "UPSTREAM_CONCLUSION": "success",
                "UPSTREAM_EVENT": "schedule",
                "UPSTREAM_HEAD_BRANCH": "develop",
                "UPSTREAM_HEAD_REPOSITORY": "Arm-Debug/amp-dev-forge",
                "UPSTREAM_HEAD_SHA": SHA,
                "UPSTREAM_RUN_ATTEMPT": "1",
                "UPSTREAM_RUN_ID": "123",
                "UPSTREAM_UPDATED_AT": "2026-08-03T10:00:00Z",
                "UPSTREAM_WORKFLOW_NAME": "Python Dependency Audit",
            }
            with patch.dict(os.environ, environment, clear=True):
                self.assertTrue(publisher.publish(root / "site"))
            saved = json.loads(
                (root / "site" / "workflow-status" / "python-audit.json").read_text(encoding="utf-8")
            )
            self.assertEqual(saved["run_id"], "123")
            self.assertIn("deploy=true", output.read_text(encoding="utf-8"))

            environment["UPSTREAM_EVENT"] = "pull_request"
            with patch.dict(os.environ, environment, clear=True):
                self.assertFalse(publisher.publish(root / "ignored"))
            self.assertFalse((root / "ignored").exists())

    def test_run_order_handles_missing_and_invalid_status(self):
        self.assertEqual(publisher.run_order(None), (0, 0))
        self.assertEqual(publisher.run_order({"run_id": "12", "run_attempt": "3"}), (12, 3))
        self.assertEqual(publisher.run_order({"run_id": "bad"}), (0, 0))

    def test_job_summary_names_failed_jobs_and_steps(self):
        jobs = {
            "jobs": [
                {
                    "name": "Raspberry Pi 5 quick-start build test",
                    "conclusion": "failure",
                    "steps": [
                        {"name": "Build", "conclusion": "success"},
                        {"name": "Browser smoke test with local data", "conclusion": "failure"},
                    ],
                },
                {"name": "Linux x86_64 quick-start build test", "conclusion": "success", "steps": []},
            ]
        }
        response = subprocess.CompletedProcess([], 0, stdout=json.dumps(jobs), stderr="")
        with patch.object(publisher.subprocess, "run", return_value=response):
            summary = publisher.job_summary("Arm-Debug/amp-dev-forge", "123", "failure")
        self.assertEqual(
            summary,
            ["Raspberry Pi 5 quick-start build test: Browser smoke test with local data"],
        )

    def test_job_summary_handles_cancelled_or_unavailable_jobs(self):
        response = subprocess.CompletedProcess([], 1, stdout="", stderr="not found")
        with patch.object(publisher.subprocess, "run", return_value=response):
            summary = publisher.job_summary("Arm-Debug/amp-dev-forge", "123", "cancelled")
        self.assertEqual(summary, ["Run cancelled before all jobs completed."])


if __name__ == "__main__":
    unittest.main()
