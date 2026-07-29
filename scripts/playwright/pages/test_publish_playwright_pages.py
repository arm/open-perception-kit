#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import base64
import datetime as dt
import importlib.util
import io
import json
import os
import sys
import tempfile
import unittest
import zipfile
from html.parser import HTMLParser
from pathlib import Path
from unittest.mock import Mock, patch


SCRIPT_PATH = Path(__file__).with_name("publish_playwright_pages.py")


def import_publish_module():
    spec = importlib.util.spec_from_file_location("publish_playwright_pages", SCRIPT_PATH)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


publish = import_publish_module()
from scripts.report_pages import publish as report_pages  # noqa: E402


class LinkParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.hrefs = []

    def handle_starttag(self, tag, attrs):
        if tag == "a":
            self.hrefs.extend(value for name, value in attrs if name == "href")


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

            self.assertTrue(changed)
            self.assertFalse(publish.decorate_playwright_report(
                report_dir,
                "Arm Perception kit",
                "../../",
                "PR #181",
                "Arm-Debug/amp-dev-forge",
                "commit-for-test",
                "{}",
            ))

    def test_decorate_playwright_report_escapes_source_map_script_tag(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            report_dir = Path(tmpdir)
            index = report_dir / "index.html"
            index.write_text(
                "<!doctype html><html><head><title>Playwright</title></head><body></body></html>",
                encoding="utf-8",
            )

            publish.decorate_playwright_report(
                report_dir,
                "Arm Perception kit",
                "../../",
                "Nightly",
                "Arm-Debug/amp-dev-forge",
                "commit-for-test",
                '{"bad":"</script><script>alert(1)</script>"}',
            )

            content = index.read_text(encoding="utf-8")
            self.assertIn('<\\/script><script>alert(1)<\\/script>', content)
            self.assertNotIn('{"bad":"</script><script>', content)

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
            nightly = site_dir / "playwright" / "nightly"
            nightly.mkdir(parents=True)
            (nightly / "index.html").write_text("<html></html>", encoding="utf-8")
            (nightly / "report-index-meta.txt").write_text(
                "develop @ commit-for-t | run 123 attempt 1 | Jul 02, 2026 20:30 UTC\n",
                encoding="utf-8",
            )
            macos_nightly = site_dir / "playwright" / "nightly-macos"
            macos_nightly.mkdir(parents=True)
            (macos_nightly / "index.html").write_text("<html></html>", encoding="utf-8")

            publish.write_site_index(site_dir, "Arm-Debug/amp-dev-forge")

            index = (site_dir / "playwright" / "index.html").read_text(encoding="utf-8")
            self.assertIn('href="nightly/index.html"', index)
            self.assertIn('href="nightly-macos/index.html"', index)
            self.assertIn('<span class="report-title">General</span>', index)
            self.assertIn('<span class="report-title">macOS</span>', index)

    def test_write_root_index_links_report_roots(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir)

            publish.write_root_index(site_dir)

            parser = LinkParser()
            parser.feed((site_dir / "index.html").read_text(encoding="utf-8"))
            self.assertEqual(parser.hrefs, [
                "yolo-benchmark/index.html",
                "yolo-imageset-benchmark/index.html",
                "playwright/index.html",
                "yolo-performance-datasets/index.html",
            ])

    def test_playwright_nightly_badge_uses_embedded_report_stats(self):
        cases = (
            ({"expected": 6, "unexpected": 0, "flaky": 0, "skipped": 0, "ok": True},
             ("fast", "6 passed")),
            ({"expected": 5, "unexpected": 1, "flaky": 0, "skipped": 0, "ok": False},
             ("slow", "1 failed")),
            ({"expected": 5, "unexpected": 0, "flaky": 1, "skipped": 0, "ok": False},
             ("neutral", "1 flaky")),
        )
        for stats, expected in cases:
            with self.subTest(stats=stats), tempfile.TemporaryDirectory() as tmpdir:
                site_dir = Path(tmpdir)
                nightly = site_dir / "playwright" / "nightly"
                nightly.mkdir(parents=True)
                buffer = io.BytesIO()
                with zipfile.ZipFile(buffer, "w") as archive:
                    archive.writestr("report.json", json.dumps({"stats": stats}))
                payload = base64.b64encode(buffer.getvalue()).decode("ascii")
                (nightly / "index.html").write_text(
                    f'<template id="playwrightReportBase64" type="application/zip">'
                    f'data:application/zip;base64,{payload}</template>',
                    encoding="utf-8",
                )

                self.assertEqual(report_pages.playwright_nightly_badge(site_dir), expected)

    def test_playwright_nightly_badge_handles_missing_or_invalid_report(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir)
            self.assertEqual(
                report_pages.playwright_nightly_badge(site_dir),
                ("neutral", "No nightly"),
            )
            nightly = site_dir / "playwright" / "nightly"
            nightly.mkdir(parents=True)
            (nightly / "index.html").write_text("<html></html>", encoding="utf-8")
            self.assertEqual(
                report_pages.playwright_nightly_badge(site_dir),
                ("neutral", "No status"),
            )

    def test_playwright_nightly_badge_aggregates_general_and_macos(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir)
            for directory, stats in (
                ("nightly", {"expected": 6, "unexpected": 0, "flaky": 0, "skipped": 0, "ok": True}),
                ("nightly-macos", {"expected": 2, "unexpected": 1, "flaky": 0, "skipped": 0, "ok": False}),
            ):
                report = site_dir / "playwright" / directory
                report.mkdir(parents=True)
                buffer = io.BytesIO()
                with zipfile.ZipFile(buffer, "w") as archive:
                    archive.writestr("report.json", json.dumps({"stats": stats}))
                payload = base64.b64encode(buffer.getvalue()).decode("ascii")
                (report / "index.html").write_text(
                    f'<template id="playwrightReportBase64" type="application/zip">'
                    f'data:application/zip;base64,{payload}</template>',
                    encoding="utf-8",
                )

            self.assertEqual(report_pages.playwright_nightly_badge(site_dir), ("slow", "1 failed"))

    def test_remove_legacy_root_site_migrates_report_roots(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir)
            (site_dir / "index.html").write_text("legacy", encoding="utf-8")
            (site_dir / "nightly").mkdir()
            (site_dir / "playwright").mkdir()
            (site_dir / "yolo-benchmark").mkdir()

            self.assertTrue(publish.remove_legacy_root_site(site_dir))

            self.assertFalse((site_dir / "index.html").exists())
            self.assertFalse((site_dir / "nightly").exists())
            self.assertTrue((site_dir / "playwright" / "nightly").is_dir())
            self.assertTrue((site_dir / "playwright").is_dir())
            self.assertTrue((site_dir / "yolo-benchmark").is_dir())

    def test_write_site_index_uses_pr_title_when_available(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir)
            pr_dir = site_dir / "playwright" / "prs" / "181"
            pr_dir.mkdir(parents=True)
            (pr_dir / "index.html").write_text("<html></html>", encoding="utf-8")
            (pr_dir / "report-index-meta.txt").write_text("branch @ commit | run 1 attempt 1\n", encoding="utf-8")

            with patch.object(publish, "pr_report_title", return_value="PR #181 - Browser smoke"):
                publish.write_site_index(site_dir, "Arm-Debug/amp-dev-forge")

            self.assertTrue((site_dir / "playwright" / "index.html").is_file())

    def test_read_first_line_uses_default_for_empty_file(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            path = Path(tmpdir) / "report-index-meta.txt"
            path.write_text("", encoding="utf-8")

            self.assertEqual(publish.read_first_line(path, "fallback"), "fallback")

    def test_source_file_list_falls_back_when_requested_sha_is_unavailable(self):
        with patch.object(publish, "run_maybe", side_effect=[Mock(returncode=1), Mock(returncode=1)]), \
                patch.object(publish, "capture") as capture:
            capture.return_value = "tests/playwright/example.spec.js\n"

            self.assertEqual(publish.source_file_list("missing-commit"), ["tests/playwright/example.spec.js"])
            capture.assert_called_once_with(["git", "ls-files"])

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

    def test_prune_report_for_pages_removes_large_binary_data(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            report_dir = Path(tmpdir)
            top_zip = report_dir / "data" / "top.zip"
            nested_zip = report_dir / "phase" / "data" / "nested.zip"
            video = report_dir / "data" / "video.webm"
            kept_json = report_dir / "data" / "trace.json"
            for path in [top_zip, nested_zip, video, kept_json]:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("data", encoding="utf-8")

            publish.prune_report_for_pages(report_dir)

            self.assertFalse(top_zip.exists())
            self.assertFalse(nested_zip.exists())
            self.assertFalse(video.exists())
            self.assertTrue(kept_json.exists())

    def test_copy_pruned_report_for_pages_keeps_source_videos_for_deploy_overlay(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            report_dir = root / "report"
            target = root / "site"
            video = report_dir / "data" / "video.webm"
            video.parent.mkdir(parents=True)
            video.write_text("video", encoding="utf-8")

            publish.copy_pruned_report_for_pages(report_dir, target)

            self.assertTrue(video.exists())
            self.assertFalse((target / "data" / "video.webm").exists())

            publish.restore_report_videos_for_deploy(report_dir, target)

            self.assertEqual((target / "data" / "video.webm").read_text(encoding="utf-8"), "video")

    def test_validate_report_for_pages_rejects_symlinks(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            report_dir = Path(tmpdir)
            target = report_dir / "target.txt"
            target.write_text("data", encoding="utf-8")
            (report_dir / "link.txt").symlink_to(target)

            with self.assertRaises(publish.PublishError):
                publish.validate_report_for_pages(report_dir)

    def test_validate_report_for_pages_rejects_oversized_reports(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            report_dir = Path(tmpdir)
            (report_dir / "large.bin").write_bytes(b"1234")

            with patch.object(publish, "MAX_REPORT_BYTES", 3), self.assertRaises(publish.PublishError):
                publish.validate_report_for_pages(report_dir)

    def test_validate_report_for_pages_ignores_deploy_only_video_size(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            report_dir = Path(tmpdir)
            video = report_dir / "data" / "video.webm"
            video.parent.mkdir(parents=True)
            video.write_bytes(b"1234")

            with patch.object(publish, "MAX_REPORT_BYTES", 3):
                publish.validate_report_for_pages(report_dir)

    def test_parse_retention_days_rejects_invalid_values(self):
        self.assertEqual(publish.parse_retention_days("10"), 10)
        with self.assertRaises(publish.PublishError):
            publish.parse_retention_days("ten")
        with self.assertRaises(publish.PublishError):
            publish.parse_retention_days("-1")

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

    def test_publish_report_skips_deploy_output_when_site_branch_is_unchanged(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            local_report = root / "playwright-report"
            site_dir = root / "site"
            output = root / "github-output"
            local_report.mkdir()
            (local_report / "index.html").write_text(
                "<!doctype html><html><head><title>Playwright</title></head><body></body></html>",
                encoding="utf-8",
            )

            def checkout(path, _storage_branch):
                path.mkdir(parents=True)

            env = {
                "GITHUB_OUTPUT": str(output),
                "GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge",
                "PLAYWRIGHT_PAGES_LOCAL_REPORT_DIR": str(local_report),
                "UPSTREAM_CONCLUSION": "success",
                "UPSTREAM_EVENT": "pull_request",
                "UPSTREAM_HEAD_BRANCH": "feature/test",
                "UPSTREAM_HEAD_REPOSITORY": "Arm-Debug/amp-dev-forge",
                "UPSTREAM_HEAD_SHA": "commit-for-test",
                "UPSTREAM_PR_NUMBER": "181",
                "UPSTREAM_RUN_ATTEMPT": "1",
                "UPSTREAM_RUN_ID": "123",
            }
            with patch.dict(os.environ, env, clear=True), \
                    patch.object(publish, "checkout_site_branch", side_effect=checkout), \
                    patch.object(publish, "build_source_map_json", return_value="{}"), \
                    patch.object(publish, "push_site_branch", return_value=False):
                publish.publish_report(site_dir, "playwright-pages")

            self.assertEqual(output.read_text(encoding="utf-8"), "deploy=false\n")

    def test_publish_macos_report_keeps_rpi_nightly(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            local_report = root / "playwright-report"
            site_dir = root / "site"
            local_report.mkdir()
            (local_report / "index.html").write_text(
                "<!doctype html><html><head><title>Playwright</title></head><body></body></html>",
                encoding="utf-8",
            )

            def checkout(path, _storage_branch):
                nightly = path / "playwright" / "nightly"
                nightly.mkdir(parents=True)
                (nightly / "marker.txt").write_text("rpi", encoding="utf-8")

            env = {
                "GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge",
                "PLAYWRIGHT_PAGES_ARTIFACT_PREFIX": "macos-browser-smoke",
                "PLAYWRIGHT_PAGES_LOCAL_REPORT_DIR": str(local_report),
                "PLAYWRIGHT_PAGES_NIGHTLY_DIRECTORY": "nightly-macos",
                "UPSTREAM_CONCLUSION": "success",
                "UPSTREAM_EVENT": "schedule",
                "UPSTREAM_HEAD_BRANCH": "develop",
                "UPSTREAM_HEAD_SHA": "commit-for-test",
                "UPSTREAM_RUN_ATTEMPT": "1",
                "UPSTREAM_RUN_ID": "123",
            }
            with patch.dict(os.environ, env, clear=True), \
                    patch.object(publish, "checkout_site_branch", side_effect=checkout), \
                    patch.object(publish, "build_source_map_json", return_value="{}"), \
                    patch.object(publish, "push_site_branch", return_value=False):
                publish.publish_report(site_dir, "playwright-pages")

            self.assertEqual(
                (site_dir / "playwright" / "nightly" / "marker.txt").read_text(encoding="utf-8"),
                "rpi",
            )
            macos = site_dir / "playwright" / "nightly-macos"
            self.assertTrue((macos / "index.html").is_file())
            self.assertEqual(
                (macos / publish.ARTIFACT_PREFIX_META).read_text(encoding="utf-8"),
                "macos-browser-smoke\n",
            )

    def test_publish_report_deploys_unchanged_site_when_videos_are_deploy_only(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            local_report = root / "playwright-report"
            site_dir = root / "site"
            output = root / "github-output"
            video = local_report / "data" / "video.webm"
            video.parent.mkdir(parents=True)
            video.write_text("video", encoding="utf-8")
            (local_report / "index.html").write_text(
                "<!doctype html><html><head><title>Playwright</title></head><body></body></html>",
                encoding="utf-8",
            )

            def checkout(path, _storage_branch):
                path.mkdir(parents=True)

            persisted = {}

            def push(path, _storage_branch):
                target = path / "playwright" / "prs" / "181"
                self.assertFalse((target / "data" / "video.webm").exists())
                persisted.update(json.loads((target / publish.VIDEO_ARTIFACT_META).read_text(encoding="utf-8")))
                return False

            env = {
                "GITHUB_OUTPUT": str(output),
                "GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge",
                "PLAYWRIGHT_PAGES_LOCAL_REPORT_DIR": str(local_report),
                "UPSTREAM_CONCLUSION": "success",
                "UPSTREAM_EVENT": "pull_request",
                "UPSTREAM_HEAD_BRANCH": "feature/test",
                "UPSTREAM_HEAD_REPOSITORY": "Arm-Debug/amp-dev-forge",
                "UPSTREAM_HEAD_SHA": "commit-for-test",
                "UPSTREAM_PR_NUMBER": "181",
                "UPSTREAM_RUN_ATTEMPT": "1",
                "UPSTREAM_RUN_ID": "123",
            }
            with patch.dict(os.environ, env, clear=True), \
                    patch.object(publish, "checkout_site_branch", side_effect=checkout), \
                    patch.object(publish, "build_source_map_json", return_value="{}"), \
                    patch.object(publish, "push_site_branch", side_effect=push):
                publish.publish_report(site_dir, "playwright-pages")

            self.assertEqual(output.read_text(encoding="utf-8"), "deploy=true\n")
            self.assertEqual(persisted, {
                "files": ["data/video.webm"],
                "run_attempt": "1",
                "run_id": "123",
            })
            self.assertEqual(
                (site_dir / "playwright" / "prs" / "181" / "data" / "video.webm").read_text(encoding="utf-8"),
                "video",
            )

    def test_publish_report_skips_stale_attempt_before_artifact_download(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            site_dir = root / "site"
            target = site_dir / "playwright" / "prs" / "181"
            output = root / "github-output"

            def checkout(_path, _storage_branch):
                target.mkdir(parents=True)
                (target / "marker.txt").write_text("newer", encoding="utf-8")
                publish.write_video_artifact_meta(target, "123", "2", ["data/video.webm"])

            env = {
                "GITHUB_OUTPUT": str(output),
                "GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge",
                "UPSTREAM_CONCLUSION": "success",
                "UPSTREAM_EVENT": "pull_request",
                "UPSTREAM_HEAD_BRANCH": "feature/test",
                "UPSTREAM_HEAD_REPOSITORY": "Arm-Debug/amp-dev-forge",
                "UPSTREAM_HEAD_SHA": "commit-for-test",
                "UPSTREAM_PR_NUMBER": "181",
                "UPSTREAM_RUN_ATTEMPT": "1",
                "UPSTREAM_RUN_ID": "123",
            }
            with patch.dict(os.environ, env, clear=True), \
                    patch.object(publish, "checkout_site_branch", side_effect=checkout), \
                    patch.object(publish, "download_report_artifact") as download, \
                    patch.object(publish, "push_site_branch") as push:
                publish.publish_report(site_dir, "playwright-pages")

            download.assert_not_called()
            push.assert_not_called()
            self.assertEqual((target / "marker.txt").read_text(encoding="utf-8"), "newer")
            self.assertEqual(output.read_text(encoding="utf-8"), "deploy=false\n")

    def test_stale_report_uses_run_attempt_and_rejects_unsafe_video_paths(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            target = Path(tmpdir)
            publish.write_video_artifact_meta(target, "123", "2", ["data/video.webm"])

            self.assertTrue(publish.is_stale_report(target, "123", "1"))
            self.assertTrue(publish.is_stale_report(target, "122", "99"))
            self.assertFalse(publish.is_stale_report(target, "123", "2"))
            self.assertFalse(publish.is_stale_report(target, "123", "3"))

            (target / publish.VIDEO_ARTIFACT_META).write_text(
                '{"files":["../video.webm"],"run_attempt":"2","run_id":"123"}',
                encoding="utf-8",
            )
            self.assertIsNone(publish.read_video_artifact_meta(target / publish.VIDEO_ARTIFACT_META))

            (target / publish.VIDEO_ARTIFACT_META).unlink()
            (target / publish.REPORT_INDEX_META).write_text(
                "branch @ commit | run 123 attempt 2 | Jul 24, 2026 10:00 UTC\n",
                encoding="utf-8",
            )
            self.assertTrue(publish.is_stale_report(target, "123", "1"))

    def test_restore_published_report_videos_restores_all_targets_best_effort(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            site_dir = Path(tmpdir)

            def report_target(relative, run_id, files=None, artifact_prefix=None):
                target = site_dir / "playwright" / relative
                target.mkdir(parents=True)
                (target / publish.REPORT_INDEX_META).write_text(
                    f"branch @ commit | run {run_id} attempt 1 | Jul 24, 2026 10:00 UTC\n",
                    encoding="utf-8",
                )
                if files is not None:
                    publish.write_video_artifact_meta(target, run_id, "1", files)
                if artifact_prefix is not None:
                    (target / publish.ARTIFACT_PREFIX_META).write_text(
                        f"{artifact_prefix}\n", encoding="utf-8"
                    )
                return target

            nightly = report_target("nightly", "100")
            macos = report_target(
                "nightly-macos", "105", ["data/macos.webm"], "macos-browser-smoke"
            )
            pr_181 = report_target("prs/181", "101", ["phase/data/pr.webm"])
            missing = report_target("prs/182", "102", ["data/missing.webm"])
            existing = report_target("prs/184", "104", ["data/existing.webm"])
            (existing / "data").mkdir()
            (existing / "data" / "existing.webm").write_text("existing", encoding="utf-8")

            downloads = []

            def download(artifact_dir, _repository, run_id, _run_attempt, artifact_prefix):
                downloads.append((run_id, artifact_prefix))
                if run_id == "102":
                    return False
                if run_id == "100":
                    relative = "data/nightly.webm"
                elif run_id == "105":
                    relative = "data/macos.webm"
                else:
                    relative = "phase/data/pr.webm"
                video = artifact_dir / "playwright-report" / relative
                video.parent.mkdir(parents=True)
                video.write_text(run_id, encoding="utf-8")
                return True

            with patch.dict(os.environ, {"GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge"}, clear=True), \
                    patch.object(publish, "download_report_artifact", side_effect=download):
                restored = publish.restore_published_report_videos(site_dir)

            self.assertEqual(restored, 3)
            self.assertEqual(downloads, [
                ("100", "rpi-browser-smoke"),
                ("105", "macos-browser-smoke"),
                ("101", "rpi-browser-smoke"),
                ("102", "rpi-browser-smoke"),
            ])
            self.assertEqual((nightly / "data" / "nightly.webm").read_text(encoding="utf-8"), "100")
            self.assertEqual((macos / "data" / "macos.webm").read_text(encoding="utf-8"), "105")
            self.assertEqual((pr_181 / "phase" / "data" / "pr.webm").read_text(encoding="utf-8"), "101")
            self.assertFalse((missing / "data" / "missing.webm").exists())
            self.assertEqual((existing / "data" / "existing.webm").read_text(encoding="utf-8"), "existing")

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
