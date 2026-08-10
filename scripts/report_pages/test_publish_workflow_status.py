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
    def test_force_deploy_does_not_require_another_storage_commit(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            environment = {
                "GITHUB_OUTPUT": str(Path(tmpdir) / "output.txt"),
                "REPORT_STATUS_FORCE_DEPLOY": "1",
            }
            with patch.dict(os.environ, environment, clear=True), patch.object(
                    publisher, "upstream_status", return_value=("python-audit", status("success"))
            ), patch.object(publisher, "checkout_site_branch"), patch.object(
                    publisher, "push_site_branch", return_value=False
            ):
                self.assertTrue(publisher.publish(Path(tmpdir) / "site"))

            self.assertIn("deploy=true", Path(environment["GITHUB_OUTPUT"]).read_text())

    def test_storage_branch_update_retries_from_scratch(self):
        attempts = 0

        def update():
            nonlocal attempts
            attempts += 1
            if attempts < 3:
                raise report_pages.StorageBranchPushError("concurrent push")
            return "published"

        self.assertEqual(report_pages.retry_storage_branch_update(update), "published")
        self.assertEqual(attempts, 3)

    def test_workflow_path_identifies_dynamic_run_name(self):
        with patch.object(publisher, "freshness_metric", return_value=("Up to date", "fast")):
            selected = publisher.status_from_run("Arm-Debug/amp-dev-forge", {
                "conclusion": "success",
                "event": "schedule",
                "head_branch": "develop",
                "head_repository": {"full_name": "Arm-Debug/amp-dev-forge"},
                "head_sha": SHA,
                "id": 123,
                "name": "Workflow dependency freshness for develop",
                "path": ".github/workflows/workflow-audit.yml",
                "pull_requests": [],
                "run_attempt": 1,
                "updated_at": "2026-08-03T10:00:00Z",
            })

        self.assertIsNotNone(selected)
        source, saved = selected
        self.assertEqual(source, "workflow-freshness")
        self.assertEqual(saved["workflow"], "Workflow Dependency Freshness")

    def test_pek_ci_run_publishes_valgrind_status_from_its_job(self):
        run = {
            "conclusion": "success",
            "event": "schedule",
            "head_branch": "develop",
            "head_repository": {"full_name": "Arm-Debug/amp-dev-forge"},
            "head_sha": SHA,
            "id": 123,
            "name": "PEK CI",
            "path": ".github/workflows/pek-ci.yml",
            "pull_requests": [],
            "run_attempt": 1,
            "updated_at": "2026-08-03T10:00:00Z",
        }
        jobs = [{"name": "Run Valgrind checks in Docker", "conclusion": "success", "steps": []}]
        with patch.object(publisher, "workflow_jobs", return_value=jobs), patch.object(
                publisher, "valgrind_metric", return_value=("140 baseline records", "neutral")
        ):
            statuses = dict(publisher.statuses_from_run("Arm-Debug/amp-dev-forge", run))

        self.assertEqual(set(statuses), {"pek-ci", "valgrind"})
        self.assertEqual(statuses["valgrind"]["metric"], "140 baseline records")
        self.assertEqual(statuses["valgrind"]["workflow"], "Valgrind Baseline Artifact")

        run.update({
            "event": "pull_request",
            "head_branch": "feature/example",
            "pull_requests": [{"number": 303}],
        })
        with patch.object(publisher, "workflow_jobs", return_value=jobs):
            statuses = publisher.statuses_from_run("Arm-Debug/amp-dev-forge", run)

        self.assertEqual([source for source, _ in statuses], ["valgrind"])
        self.assertEqual(statuses[0][1]["metric"], "0 new errors")

    def test_pek_ci_pr_run_publishes_embedded_audit_statuses(self):
        run = {
            "conclusion": "success",
            "event": "pull_request",
            "head_branch": "feature/example",
            "head_repository": {"full_name": "Arm-Debug/amp-dev-forge"},
            "head_sha": SHA,
            "id": 123,
            "name": "PEK CI",
            "path": ".github/workflows/pek-ci.yml",
            "pull_requests": [{"number": 305}],
            "run_attempt": 1,
            "updated_at": "2026-08-03T10:00:00Z",
        }
        jobs = [
            {"name": "Python Dependency Audit / pip-audit (expkits-ci)",
             "conclusion": "success", "steps": []},
            {"name": "Python Dependency Audit / pip-audit (plumber)",
             "conclusion": "success", "steps": []},
            {"name": "Docker Scout Image Audit / docker-scout (pek-ci)",
             "conclusion": "failure", "steps": []},
            {"name": "Workflow Dependency Freshness / workflow dependency freshness",
             "conclusion": "success", "steps": []},
        ]
        with patch.object(publisher, "workflow_jobs", return_value=jobs), patch.object(
                publisher, "docker_scout_metric", return_value=("1 critical", "slow", [])
        ), patch.object(
                publisher, "freshness_metric", return_value=("Up to date", "fast")
        ):
            statuses = dict(publisher.statuses_from_run("Arm-Debug/amp-dev-forge", run))

        self.assertEqual(
            set(statuses), {"python-audit", "docker-scout", "workflow-freshness"}
        )
        self.assertEqual(statuses["python-audit"]["metric"], "2/2 clean")
        self.assertEqual(statuses["docker-scout"]["conclusion"], "failure")
        self.assertEqual(statuses["docker-scout"]["metric"], "1 critical")
        self.assertEqual(statuses["workflow-freshness"]["metric"], "Up to date")

    def test_skipped_valgrind_reports_only_shared_image_failures(self):
        run = {
            "conclusion": "failure",
            "event": "pull_request",
            "head_branch": "feature/example",
            "head_repository": {"full_name": "Arm-Debug/amp-dev-forge"},
            "head_sha": SHA,
            "id": 123,
            "name": "PEK CI",
            "path": ".github/workflows/pek-ci.yml",
            "pull_requests": [{"number": 305}],
            "run_attempt": 1,
            "updated_at": "2026-08-03T10:00:00Z",
        }
        valgrind = {"name": "Run Valgrind checks in Docker", "conclusion": "skipped", "steps": []}

        jobs = [
            {"name": "Build Docker image", "conclusion": "success", "steps": []},
            valgrind,
            {"name": "Docker Scout Image Audit / docker-scout (pek-ci)",
             "conclusion": "failure", "steps": []},
        ]
        with patch.object(publisher, "workflow_jobs", return_value=jobs), patch.object(
                publisher, "docker_scout_metric", return_value=("1 critical", "slow", [])
        ):
            statuses = dict(publisher.statuses_from_run("Arm-Debug/amp-dev-forge", run))
        self.assertNotIn("valgrind", statuses)
        self.assertEqual(statuses["docker-scout"]["conclusion"], "failure")

        for build_job_name in ("Build PEK CI image", "Build Docker image"):
            with self.subTest(build_job_name=build_job_name):
                jobs = [
                    {"name": build_job_name, "conclusion": "failure", "steps": []},
                    valgrind,
                ]
                with patch.object(publisher, "workflow_jobs", return_value=jobs):
                    statuses = dict(
                        publisher.statuses_from_run("Arm-Debug/amp-dev-forge", run)
                    )
                self.assertEqual(statuses["valgrind"]["conclusion"], "failure")

    def test_scheduled_publish_reconciles_latest_source_statuses(self):
        python_status = status("success")
        python_status["workflow"] = "Python Dependency Audit"
        freshness_status = status("success")
        freshness_status.update({
            "run_id": "124",
            "workflow": "Workflow Dependency Freshness",
        })
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir) / "site"
            environment = {
                "GITHUB_OUTPUT": str(Path(tmpdir) / "output.txt"),
                "REPORT_STATUS_PAGES_DRY_RUN": "1",
                "REPORT_STATUS_RECONCILE_SCHEDULED": "1",
            }
            with patch.dict(os.environ, environment, clear=True):
                with patch.object(
                        publisher, "upstream_status",
                        return_value=("python-audit", python_status)), patch.object(
                        publisher,
                        "latest_scheduled_statuses",
                        return_value=[("workflow-freshness", freshness_status)],
                ):
                    self.assertTrue(publisher.publish(site_dir))

            self.assertTrue((
                site_dir / "workflow-status" / "python-audit" / "nightly.json"
            ).is_file())
            self.assertTrue((
                site_dir / "workflow-status" / "workflow-freshness" / "nightly.json"
            ).is_file())

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
                "details": [
                    "pek-ci: 3 critical · 56 high",
                    "pek-dev: 3 critical · 59 high",
                ],
            })
            fixtures["workflow-freshness"].update({"metric": "1 behind", "metric_tone": "neutral"})
            fixtures["valgrind"].update({
                "metric": "140 baseline records",
                "workflow": "Valgrind Baseline Artifact",
            })
            for source, payload in fixtures.items():
                target = (
                    status_dir / f"{source}.json"
                    if source == "workflow-freshness"
                    else status_dir / source / "nightly.json"
                )
                target.parent.mkdir(exist_ok=True)
                target.write_text(json.dumps(payload), encoding="utf-8")

            report_pages.write_root_index(site_dir, now=NOW)
            index = (site_dir / "index.html").read_text(encoding="utf-8")
            nightly = (site_dir / "nightly-ci" / "index.html").read_text(encoding="utf-8")
            nightly_css = (site_dir / "nightly-ci" / "report-index.css").read_text(encoding="utf-8")
            python_audit = (site_dir / "python-audit" / "index.html").read_text(encoding="utf-8")
            docker_scout = (site_dir / "docker-scout" / "index.html").read_text(encoding="utf-8")
            valgrind = (site_dir / "valgrind" / "index.html").read_text(encoding="utf-8")

        for label in ("Passed", "1/2 failed", "10 critical · 160 high", "Stale"):
            self.assertIn(f">{label}<", nightly)
        self.assertIn('href="nightly-ci/index.html"', index)
        self.assertIn('href="python-audit/index.html"', index)
        self.assertIn(".verdict-slow", nightly_css)
        self.assertNotIn("<h2>Nightly CI</h2>", index)
        self.assertEqual(index.count('<section class="nightly-status">'), 2)
        self.assertIn("3 need attention", index)
        for metric in ("1/2 failed", "10 critical · 160 high", "1 behind",
                       "140 baseline records"):
            self.assertIn(metric, index)
        self.assertIn("Aug 03, 2026 10:00 UTC", python_audit)
        self.assertIn("/actions/runs/123", python_audit)
        self.assertIn("pip-audit &lt;expkits-ci&gt;: Run pip-audit", python_audit)
        self.assertIn("pek-ci: 3 critical · 56 high", docker_scout)
        self.assertNotIn("pek-ci: 3 critical · 56 high", nightly)
        self.assertIn("<h2>Nightly</h2>", valgrind)
        self.assertIn(">Job summary</a>", valgrind)

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
                    patch.object(publisher, "valgrind_metric") as valgrind_metric:
                self.assertFalse(publisher.publish(root / "valgrind-site"))
            valgrind_metric.assert_not_called()
            self.assertFalse((root / "valgrind-site").exists())

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
            self.assertFalse((site_dir / "nightly").exists())
            playwright_index = (site_dir / "playwright" / "index.html").read_text()
            self.assertIn('href="nightly/index.html"', playwright_index)
            self.assertTrue((site_dir / "nightly-ci" / "index.html").is_file())

    def test_status_publish_rejects_stale_run_against_legacy_status(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir) / "site"
            legacy = site_dir / "workflow-status" / "python-audit.json"
            legacy.parent.mkdir(parents=True)
            legacy.write_text(json.dumps({"run_id": "124", "run_attempt": "1"}))
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
                    patch.object(publisher, "workflow_jobs", return_value=[]), \
                    patch.object(publisher, "push_site_branch") as push:
                self.assertFalse(publisher.publish(site_dir))

            push.assert_not_called()
            self.assertFalse((legacy.parent / "python-audit" / "nightly.json").exists())

    def test_nightly_overview_links_runs_and_lists_unavailable_sources(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir)
            report_pages.write_root_index(site_dir, now=NOW)
            nightly = (site_dir / "nightly-ci" / "index.html").read_text(encoding="utf-8")

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
            "name": "Quick-start and Playwright on Raspberry Pi 5",
            "conclusion": "failure",
            "steps": [
                {"name": "Build", "conclusion": "success"},
                {"name": "Browser smoke test with local data", "conclusion": "failure"},
            ],
        }, {"name": "Quick-start on Linux", "conclusion": "success", "steps": []}]
        summary = publisher.job_summary(jobs, "failure")
        self.assertEqual(
            summary,
            ["Quick-start and Playwright on Raspberry Pi 5: Browser smoke test with local data"],
        )

    def test_job_summary_handles_cancelled_or_unavailable_jobs(self):
        summary = publisher.job_summary([], "cancelled")
        self.assertEqual(summary, ["Run cancelled before all jobs completed."])

    def test_workflow_metrics_use_job_counts_and_valgrind_comparison(self):
        jobs = [{"conclusion": "failure"}, {"conclusion": "success"}]
        self.assertEqual(
            publisher.workflow_metric("python-audit", "schedule", "failure", jobs, "repo", "1"),
            ("1/2 failed", "", []),
        )
        with patch.object(publisher, "docker_scout_metric", return_value=("", "", [])):
            self.assertEqual(
                publisher.workflow_metric(
                    "docker-scout", "schedule", "failure", jobs, "repo", "1"
                ),
                ("1/2 incomplete", "", []),
            )
        self.assertEqual(
            publisher.workflow_metric("valgrind", "pull_request", "success", [], "repo", "1"),
            ("0 new errors", "fast", []),
        )
        for source in ("python-audit", "docker-scout"):
            self.assertEqual(
                publisher.workflow_metric(
                    source, "schedule", "cancelled", [{"conclusion": "cancelled"}], "repo", "1"
                ),
                ("", "", []),
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
                    "service": f"image-{index}",
                    "sarif_present": True,
                    "severity_counts": counts,
                }), encoding="utf-8")
                reports.append(path)

            self.assertEqual(
                publisher.docker_scout_report_metric(reports, expected_reports=2),
                (
                    "3 critical · 66 high",
                    "slow",
                    ["image-0: 1 critical · 14 high", "image-1: 2 critical · 52 high"],
                ),
            )
            self.assertEqual(
                publisher.docker_scout_report_metric(reports, expected_reports=3),
                ("", "", []),
            )

    def test_valgrind_metric_reads_explicit_producer_value(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            report = Path(tmpdir) / "report.xml"
            report.write_text(
                '<valgrindoutput collected_errors="140"/>', encoding="utf-8"
            )

            self.assertEqual(
                publisher.valgrind_report_metric(report),
                ("140 baseline records", "neutral"),
            )


if __name__ == "__main__":
    unittest.main()
