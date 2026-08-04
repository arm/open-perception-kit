################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################
import datetime as dt
import json
import os
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
        "event": "schedule",
        "head_branch": "develop",
        "head_sha": SHA,
        "pull_request_number": "",
        "repository": "Arm-Debug/amp-dev-forge",
        "run_attempt": "1",
        "run_id": "123",
        "updated_at": updated_at,
        "workflow": "Example",
    }


class TestPublishWorkflowStatus(unittest.TestCase):
    def test_root_and_report_indexes_render_status_without_homepage_duplication(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir)
            status_dir = site_dir / report_pages.WORKFLOW_STATUS_DIRECTORY
            status_dir.mkdir()
            fixtures = {
                "pek-ci": status("success"),
                "python-audit": status("failure"),
                "docker-scout": status("failure"),
                "workflow-freshness": status("success"),
                "yolo-video": status("success", "2026-08-01T00:00:00Z"),
                "yolo-imageset": status("success"),
                "valgrind": status("success"),
            }
            fixtures["python-audit"]["summary"] = ["pip-audit <expkits-ci>: Run pip-audit"]
            fixtures["python-audit"]["metric"] = "1/2 failed"
            fixtures["docker-scout"].update({
                "metric": "10 critical · 160 high",
                "metric_tone": "slow",
            })
            fixtures["workflow-freshness"].update({"metric": "1 behind", "metric_tone": "neutral"})
            fixtures["valgrind"].update({
                "event": "push",
                "metric": "18 repo-owned baseline",
            })
            for source, payload in fixtures.items():
                target = (
                    status_dir / f"{source}.json"
                    if source == "workflow-freshness"
                    else status_dir / source / ("develop.json" if source == "valgrind" else "nightly.json")
                )
                target.parent.mkdir(exist_ok=True)
                target.write_text(json.dumps(payload), encoding="utf-8")

            report_pages.write_root_index(site_dir, now=NOW)
            index = (site_dir / "index.html").read_text(encoding="utf-8")
            nightly = (site_dir / "nightly" / "index.html").read_text(encoding="utf-8")
            nightly_css = (site_dir / "nightly" / "report-index.css").read_text(encoding="utf-8")
            python_audit = (site_dir / "python-audit" / "index.html").read_text(encoding="utf-8")
            valgrind = (site_dir / "valgrind" / "index.html").read_text(encoding="utf-8")

        for label in ("Passed", "1/2 failed", "10 critical · 160 high", "Stale"):
            self.assertIn(f">{label}<", nightly)
        self.assertIn('href="nightly/index.html"', index)
        self.assertIn('href="python-audit/index.html"', index)
        self.assertIn(".verdict-slow", nightly_css)
        self.assertNotIn("<h2>Nightly CI</h2>", index)
        self.assertEqual(index.count('<section class="nightly-status">'), 2)
        self.assertIn("3 need attention", index)
        for metric in ("1/2 failed", "10 critical · 160 high", "1 behind",
                       "18 repo-owned baseline"):
            self.assertIn(metric, index)
        self.assertIn("Aug 03, 2026 10:00 UTC", python_audit)
        self.assertIn("/actions/runs/123", python_audit)
        self.assertIn("pip-audit &lt;expkits-ci&gt;: Run pip-audit", python_audit)
        self.assertIn("<h2>Develop</h2>", valgrind)
        self.assertNotIn("Repository-owned Valgrind baseline", valgrind)

    def test_publish_persists_develop_schedule_and_pull_request(self):
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
            with patch.dict(os.environ, environment, clear=True), \
                    patch.object(publisher, "workflow_jobs", return_value=[]):
                self.assertTrue(publisher.publish(root / "site"))
            saved = json.loads((
                root / "site" / "workflow-status" / "python-audit" / "nightly.json"
            ).read_text(encoding="utf-8"))
            self.assertEqual(saved["run_id"], "123")
            self.assertIn("deploy=true", output.read_text(encoding="utf-8"))

            environment["UPSTREAM_EVENT"] = "push"
            with patch.dict(os.environ, environment, clear=True), \
                    patch.object(publisher, "workflow_jobs", return_value=[]):
                self.assertFalse(publisher.publish(root / "ignored"))
            self.assertFalse((root / "ignored").exists())

            environment.update({
                "UPSTREAM_EVENT": "pull_request",
                "UPSTREAM_HEAD_BRANCH": "feature/EXPKITS-1182/report-failure-summaries",
                "UPSTREAM_PULL_REQUEST_NUMBER": "271",
            })
            with patch.dict(os.environ, environment, clear=True), \
                    patch.object(publisher, "workflow_jobs", return_value=[]):
                self.assertTrue(publisher.publish(root / "pr-site"))
            self.assertTrue((
                root / "pr-site" / "workflow-status" / "python-audit" / "prs" / "271.json"
            ).is_file())

            environment.update({
                "UPSTREAM_EVENT": "push",
                "UPSTREAM_HEAD_BRANCH": "develop",
                "UPSTREAM_PULL_REQUEST_NUMBER": "",
                "UPSTREAM_WORKFLOW_NAME": "Valgrind Baseline Artifact",
            })
            with patch.dict(os.environ, environment, clear=True), \
                    patch.object(publisher, "workflow_jobs", return_value=[]), \
                    patch.object(
                        publisher,
                        "valgrind_metric",
                        return_value=("18 repo-owned baseline", "neutral"),
            ):
                self.assertTrue(publisher.publish(root / "valgrind-site"))
            valgrind_status = (
                root / "valgrind-site" / "workflow-status" / "valgrind" / "develop.json"
            )
            self.assertTrue(valgrind_status.is_file())

    def test_status_publish_migrates_legacy_playwright_root(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir) / "site"
            legacy = site_dir / "nightly" / "index.html"
            legacy.parent.mkdir(parents=True)
            legacy.write_text("legacy Playwright", encoding="utf-8")
            environment = {
                "GITHUB_OUTPUT": str(Path(tmpdir) / "output.txt"),
                "GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge",
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
            with patch.dict(os.environ, environment, clear=True), \
                    patch.object(publisher, "checkout_site_branch"), \
                    patch.object(publisher, "push_site_branch", return_value=True), \
                    patch.object(publisher, "workflow_jobs", return_value=[]):
                self.assertTrue(publisher.publish(site_dir))

            self.assertEqual(
                (site_dir / "playwright" / "nightly" / "index.html").read_text(),
                "legacy Playwright",
            )

    def test_nightly_overview_links_runs_and_lists_unavailable_sources(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir)
            report_pages.write_root_index(site_dir, now=NOW)
            nightly = (site_dir / "nightly" / "index.html").read_text(encoding="utf-8")

        self.assertIn("PEK CI", nightly)
        self.assertIn("Unavailable", nightly)
        rendered = report_pages.status_link(status("failure"), "PEK CI", NOW, "../playwright/")
        self.assertIn("/actions/runs/123", rendered)
        self.assertIn('href="../playwright/"', rendered)

    def test_run_order_handles_missing_and_invalid_status(self):
        self.assertEqual(publisher.run_order(None), (0, 0))
        self.assertEqual(publisher.run_order({"run_id": "12", "run_attempt": "3"}), (12, 3))
        self.assertEqual(publisher.run_order({"run_id": "bad"}), (0, 0))

    def test_job_summary_names_failed_jobs_and_steps(self):
        jobs = [{
            "name": "Raspberry Pi 5 quick-start build test",
            "conclusion": "failure",
            "steps": [
                {"name": "Build", "conclusion": "success"},
                {"name": "Browser smoke test with local data", "conclusion": "failure"},
            ],
        }, {"name": "Linux x86_64 quick-start build test", "conclusion": "success", "steps": []}]
        summary = publisher.job_summary(jobs, "failure")
        self.assertEqual(
            summary,
            ["Raspberry Pi 5 quick-start build test: Browser smoke test with local data"],
        )

    def test_job_summary_handles_cancelled_or_unavailable_jobs(self):
        summary = publisher.job_summary([], "cancelled")
        self.assertEqual(summary, ["Run cancelled before all jobs completed."])

    def test_valgrind_develop_status_uses_baseline_artifact_lifetime(self):
        valgrind = status("success", "2026-08-01T00:00:00Z")
        valgrind.update({
            "event": "push",
            "workflow": "Valgrind Baseline Artifact",
            "metric": "18 repo-owned baseline",
            "metric_tone": "neutral",
        })

        self.assertEqual(
            report_pages.workflow_status_badge(status("success", "2026-08-01T00:00:00Z"), NOW),
            ("neutral", "Stale"),
        )
        self.assertEqual(
            report_pages.workflow_status_badge(valgrind, NOW),
            ("neutral", "18 repo-owned baseline"),
        )

    def test_workflow_metrics_use_job_counts_and_valgrind_comparison(self):
        jobs = [{"conclusion": "failure"}, {"conclusion": "success"}]
        self.assertEqual(
            publisher.workflow_metric("python-audit", "schedule", "failure", jobs, "repo", "1"),
            ("1/2 failed", ""),
        )
        with patch.object(publisher, "docker_scout_metric", return_value=("", "")):
            self.assertEqual(
                publisher.workflow_metric(
                    "docker-scout", "schedule", "failure", jobs, "repo", "1"
                ),
                ("1/2 incomplete", ""),
            )
        self.assertEqual(
            publisher.workflow_metric("valgrind", "pull_request", "success", [], "repo", "1"),
            ("0 new errors", "fast"),
        )
        for source in ("python-audit", "docker-scout"):
            self.assertEqual(
                publisher.workflow_metric(
                    source, "schedule", "cancelled", [{"conclusion": "cancelled"}], "repo", "1"
                ),
                ("", ""),
            )

    def test_docker_scout_metric_aggregates_explicit_producer_counts(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            reports = []
            for index, counts in enumerate((
                {"critical": 1, "high": 14, "medium": 11, "low": 115, "unspecified": 33},
                {"critical": 2, "high": 52, "medium": 64, "low": 293, "unspecified": 107},
            )):
                path = Path(tmpdir) / f"report-{index}.json"
                path.write_text(json.dumps({
                    "sarif_present": True,
                    "severity_counts": counts,
                }), encoding="utf-8")
                reports.append(path)

            self.assertEqual(
                publisher.docker_scout_report_metric(reports, expected_reports=2),
                ("3 critical · 66 high", "slow"),
            )
            self.assertEqual(
                publisher.docker_scout_report_metric(reports, expected_reports=3),
                ("", ""),
            )

    def test_valgrind_metric_reads_explicit_producer_value(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            report = Path(tmpdir) / "report.xml"
            report.write_text(
                '<valgrindoutput repo_owned_errors="18"/>', encoding="utf-8"
            )

            self.assertEqual(
                publisher.valgrind_report_metric(report),
                ("18 repo-owned baseline", "neutral"),
            )


if __name__ == "__main__":
    unittest.main()
