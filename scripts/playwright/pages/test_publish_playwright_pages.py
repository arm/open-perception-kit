#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import datetime as dt
import importlib.util
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


SCRIPT_PATH = Path(__file__).with_name("publish_playwright_pages.py")


def import_publish_module():
    spec = importlib.util.spec_from_file_location("publish_playwright_pages", SCRIPT_PATH)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


publish = import_publish_module()


class TestPublishPlaywrightPages(unittest.TestCase):
    def test_build_source_map_links_unique_basenames(self):
        source_map = publish.build_source_map([
            "tests/playwright/pek-browser-models.spec.js",
            "development/web/content/models.js",
            "other/models.js",
            "README.md",
            "build/output.o",
        ])

        self.assertEqual(
            source_map["pek-browser-models.spec.js"],
            "tests/playwright/pek-browser-models.spec.js",
        )
        self.assertNotIn("models.js", source_map)
        self.assertEqual(source_map["README.md"], "README.md")
        self.assertNotIn("build/output.o", source_map)

    def test_decorate_playwright_report_injects_expected_shell(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            report_dir = Path(tmpdir)
            index = report_dir / "index.html"
            index.write_text(
                "<!doctype html><html><head><title>Playwright</title></head><body><main></main></body></html>",
                encoding="utf-8",
            )

            changed = publish.decorate_playwright_report(
                report_dir,
                "Arm Perception kit",
                "../../",
                'PR #181 | <a href="https://example.invalid/run">run</a>',
                "Arm-Debug/amp-dev-forge",
                "commit-for-test",
                '{"pek-browser-models.spec.js":"tests/playwright/pek-browser-models.spec.js"}',
            )

            content = index.read_text(encoding="utf-8")
            self.assertTrue(changed)
            self.assertIn("<title>Arm Perception kit - Playwright report</title>", content)
            self.assertIn('<link rel="stylesheet" href="../../report-shell.css">', content)
            self.assertIn('<script src="../../report-shell.js" defer></script>', content)
            self.assertIn('class="pek-report-bar"', content)
            self.assertIn('href="../../index.html"', content)
            self.assertIn('data-repository="Arm-Debug/amp-dev-forge"', content)
            self.assertIn('data-commit="commit-for-test"', content)
            self.assertIn('id="pek-report-source-map"', content)

    def test_decorate_playwright_report_fails_when_report_markup_moves(self):
        cases = {
            "title": "<!doctype html><html><head></head><body></body></html>",
            "head": "<!doctype html><html><head><title>Playwright</title></html>",
            "body": "<!doctype html><html><head><title>Playwright</title></head></html>",
        }

        for name, html in cases.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as tmpdir:
                report_dir = Path(tmpdir)
                (report_dir / "index.html").write_text(html, encoding="utf-8")

                with self.assertRaises(publish.PublishError):
                    publish.decorate_playwright_report(
                        report_dir,
                        "Arm Perception kit",
                        "../",
                        "Nightly",
                        "Arm-Debug/amp-dev-forge",
                        "commit-for-test",
                        "{}",
                    )

    def test_write_site_index_handles_nightly_without_pr_directory(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir)
            nightly = site_dir / "nightly"
            nightly.mkdir()
            (nightly / "index.html").write_text("<html></html>", encoding="utf-8")
            (nightly / "report-index-meta.txt").write_text(
                "main @ commit-for-t | run 123 attempt 1 | Jul 02, 2026 20:30 UTC\n",
                encoding="utf-8",
            )

            publish.write_site_index(site_dir, "Arm-Debug/amp-dev-forge")

            content = (site_dir / "index.html").read_text(encoding="utf-8")
            self.assertIn("Latest nightly", content)
            self.assertIn("main @ commit-for-t", content)
            self.assertIn("Pull Requests", content)

    def test_write_site_index_uses_pr_title_when_available(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir)
            pr_dir = site_dir / "prs" / "181"
            pr_dir.mkdir(parents=True)
            (pr_dir / "index.html").write_text("<html></html>", encoding="utf-8")
            (pr_dir / "report-index-meta.txt").write_text("branch @ commit | run 1 attempt 1\n", encoding="utf-8")

            with patch.object(publish, "pr_report_title", return_value="PR #181 - Browser smoke"):
                publish.write_site_index(site_dir, "Arm-Debug/amp-dev-forge")

            content = (site_dir / "index.html").read_text(encoding="utf-8")
            self.assertIn("PR #181 - Browser smoke", content)
            self.assertIn('href="prs/181/"', content)

    def test_report_index_meta_text_is_human_readable(self):
        now = dt.datetime(2026, 7, 2, 20, 30, tzinfo=dt.timezone.utc)

        text = publish.build_report_index_meta_text(
            "feature/example",
            "commit-for-test",
            "123",
            "2",
            now=now,
        )

        self.assertEqual(text, "feature/example @ commit-for-t | run 123 attempt 2 | Jul 02, 2026 20:30 UTC")

    def test_should_prune_closed_pr_after_retention_cutoff(self):
        cutoff = dt.datetime(2026, 7, 2, tzinfo=dt.timezone.utc)

        self.assertTrue(publish.should_prune_closed_pr("MERGED", "2026-07-01T23:59:00Z", cutoff))
        self.assertFalse(publish.should_prune_closed_pr("MERGED", "2026-07-02T00:01:00Z", cutoff))
        self.assertFalse(publish.should_prune_closed_pr("OPEN", "2026-07-01T23:59:00Z", cutoff))
        self.assertFalse(publish.should_prune_closed_pr("CLOSED", "", cutoff))

    def test_download_report_artifact_uses_local_report_dir(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            local_report = root / "playwright-report"
            local_report.mkdir()
            (local_report / "index.html").write_text("<html></html>", encoding="utf-8")

            with patch.dict(os.environ, {"PLAYWRIGHT_PAGES_LOCAL_REPORT_DIR": str(local_report)}):
                self.assertTrue(publish.download_report_artifact(root / "artifact", "repo", "run", "attempt"))

            self.assertTrue((root / "artifact" / "local-artifact" / "playwright-report" / "index.html").is_file())

    def test_dry_run_site_branch_does_not_need_github_token(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir)
            with patch.dict(os.environ, {"PLAYWRIGHT_PAGES_DRY_RUN": "1"}, clear=True):
                publish.checkout_site_branch(site_dir, "local-pages")
                (site_dir / "index.html").write_text("<html></html>", encoding="utf-8")

                self.assertTrue(publish.push_site_branch(site_dir, "local-pages"))
                self.assertTrue((site_dir / ".git").is_dir())


if __name__ == "__main__":
    unittest.main()
