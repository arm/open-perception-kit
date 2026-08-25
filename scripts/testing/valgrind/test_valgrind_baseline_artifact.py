#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import hashlib
import importlib.util
import os
import tempfile
import unittest
import urllib.error
from pathlib import Path
from unittest import mock


SCRIPT_PATH = Path(__file__).with_name("valgrind-baseline-artifact.py")
TARGET_BRANCH_NAME = "develop"
BASE_SHA = "a" * 40
OLD_SHA = "b" * 40


def load_helper():
    spec = importlib.util.spec_from_file_location("valgrind_baseline_artifact", SCRIPT_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class TestValgrindBaselineArtifact(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.helper = load_helper()

    @staticmethod
    def summary(path: Path, obj: str = "/work/lib.so") -> bytes:
        content = (
            "<valgrindoutput><error><kind>Leak</kind><stack><frame>"
            f"<obj>{obj}</obj><fn>test</fn>"
            "</frame></stack></error></valgrindoutput>"
        ).encode()
        path.write_bytes(content)
        return content

    def test_download_writes_the_current_branch_artifact(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir)
            expected = output / self.helper.SUMMARY_NAME
            content = self.summary(output / "source.xml")

            with mock.patch.object(
                self.helper, "read_remote_baseline", return_value=(BASE_SHA, content)
            ):
                code = self.helper.download_baseline(TARGET_BRANCH_NAME, BASE_SHA, output)

            self.assertEqual(code, 0)
            self.assertEqual(self.helper.summary_digest(expected), self.helper.summary_digest_bytes(content))

    def test_missing_branch_baseline_removes_a_stale_local_artifact(self):
        with tempfile.TemporaryDirectory() as tmpdir, mock.patch.object(
            self.helper, "read_remote_baseline", return_value=None
        ):
            output = Path(tmpdir)
            expected = output / self.helper.SUMMARY_NAME
            expected.write_text("stale", encoding="utf-8")
            code = self.helper.download_baseline(TARGET_BRANCH_NAME, BASE_SHA, output)

        self.assertEqual(code, 1)
        self.assertFalse(expected.exists())

    def test_outdated_branch_baseline_is_not_used(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir)
            content = self.summary(output / "source.xml")
            with mock.patch.object(
                self.helper, "read_remote_baseline", return_value=(OLD_SHA, content)
            ):
                code = self.helper.download_baseline(TARGET_BRANCH_NAME, BASE_SHA, output)

        self.assertEqual(code, 1)
        self.assertFalse((output / self.helper.SUMMARY_NAME).exists())

    def test_invalid_download_removes_a_stale_local_artifact(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir)
            expected = output / self.helper.SUMMARY_NAME
            expected.write_text("stale", encoding="utf-8")
            response = mock.MagicMock()
            response.__enter__.return_value.read.return_value = b"<not-valgrindoutput />"
            with mock.patch.object(
                self.helper,
                "artifactory_request",
                return_value=response,
            ):
                code = self.helper.download_baseline(TARGET_BRANCH_NAME, BASE_SHA, output)

            self.assertEqual(code, 1)
            self.assertFalse(expected.exists())

    def test_upload_skips_a_source_that_is_no_longer_branch_head(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            summary = Path(tmpdir) / self.helper.SUMMARY_NAME
            self.summary(summary)
            with mock.patch.object(
                self.helper, "get_target_branch_head_sha", return_value=OLD_SHA
            ), mock.patch.object(self.helper, "read_remote_baseline") as read_remote:
                code = self.helper.upload_baseline(TARGET_BRANCH_NAME, BASE_SHA, summary)

        self.assertEqual(code, 0)
        read_remote.assert_not_called()

    def test_upload_skips_existing_identical_baseline(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            summary = Path(tmpdir) / self.helper.SUMMARY_NAME
            content = self.summary(summary)
            with mock.patch.object(
                self.helper, "get_target_branch_head_sha", return_value=BASE_SHA
            ), mock.patch.object(
                self.helper, "read_remote_baseline", return_value=(BASE_SHA, content)
            ), mock.patch.object(self.helper, "artifactory_request") as request:
                code = self.helper.upload_baseline(TARGET_BRANCH_NAME, BASE_SHA, summary)

        self.assertEqual(code, 0)
        request.assert_not_called()

    def test_upload_rejects_conflicting_baseline_for_the_same_head(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            summary = Path(tmpdir) / self.helper.SUMMARY_NAME
            self.summary(summary)
            other = Path(tmpdir) / "other.xml"
            existing = self.summary(other, "/work/other.so")
            with mock.patch.object(
                self.helper, "get_target_branch_head_sha", return_value=BASE_SHA
            ), mock.patch.object(
                self.helper, "read_remote_baseline", return_value=(BASE_SHA, existing)
            ):
                with self.assertRaisesRegex(RuntimeError, "Conflicting"):
                    self.helper.upload_baseline(TARGET_BRANCH_NAME, BASE_SHA, summary)

    def test_upload_replaces_the_previous_branch_head(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            summary = Path(tmpdir) / self.helper.SUMMARY_NAME
            content = self.summary(summary)
            with mock.patch.object(
                self.helper,
                "get_target_branch_head_sha",
                side_effect=[BASE_SHA, BASE_SHA],
            ), mock.patch.object(
                self.helper,
                "read_remote_baseline",
                side_effect=[(OLD_SHA, content), (BASE_SHA, content)],
            ), mock.patch.object(
                self.helper, "artifactory_request", return_value=mock.MagicMock()
            ) as request:
                code = self.helper.upload_baseline(TARGET_BRANCH_NAME, BASE_SHA, summary)

        self.assertEqual(code, 0)
        request.assert_called_once_with(TARGET_BRANCH_NAME, data=mock.ANY)

    def test_upload_retries_transient_artifactory_failure(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            summary = Path(tmpdir) / self.helper.SUMMARY_NAME
            content = self.summary(summary)
            uploads = 0

            def request(*_args, **_kwargs):
                nonlocal uploads
                uploads += 1
                if uploads == 1:
                    raise urllib.error.URLError("temporary")
                return mock.MagicMock()

            with mock.patch.object(
                self.helper,
                "get_target_branch_head_sha",
                side_effect=[BASE_SHA, BASE_SHA],
            ), mock.patch.object(
                self.helper,
                "read_remote_baseline",
                side_effect=[None, (BASE_SHA, content)],
            ), mock.patch.object(
                self.helper, "artifactory_request", side_effect=request
            ), mock.patch.object(self.helper.time, "sleep"):
                code = self.helper.upload_baseline(TARGET_BRANCH_NAME, BASE_SHA, summary)

        self.assertEqual(code, 0)
        self.assertEqual(uploads, 2)

    def test_artifactory_request_uses_branch_path_auth_and_upload_checksum(self):
        content = b"summary"
        with mock.patch.dict(
            os.environ,
            {"ARTIFACTORY_USER": "ci-user", "ARTIFACTORY_TOKEN": "ci-token"},
        ), mock.patch.object(
            self.helper.urllib.request,
            "urlopen",
            return_value=mock.MagicMock(),
        ) as urlopen:
            self.helper.artifactory_request("feature/test", data=content)

        request = urlopen.call_args.args[0]
        self.assertEqual(request.get_method(), "PUT")
        self.assertEqual(
            request.full_url,
            f"{self.helper.ARTIFACTORY_BASE_URL}/feature/test/{self.helper.SUMMARY_NAME}",
        )
        self.assertTrue(request.get_header("Authorization").startswith("Basic "))
        self.assertEqual(
            request.get_header("X-checksum-sha256"), hashlib.sha256(content).hexdigest()
        )

    def test_read_remote_baseline_treats_404_as_missing(self):
        missing = urllib.error.HTTPError("url", 404, "missing", {}, None)
        with mock.patch.object(self.helper, "artifactory_request", side_effect=missing):
            self.assertIsNone(self.helper.read_remote_baseline(TARGET_BRANCH_NAME))

    def test_digest_ignores_non_repository_errors(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            first = Path(tmpdir) / "first.xml"
            second = Path(tmpdir) / "second.xml"
            repository = Path(tmpdir) / "repository.xml"
            self.summary(first, "/usr/lib/a.so")
            self.summary(second, "/usr/lib/b.so")
            self.summary(repository)

            self.assertEqual(
                self.helper.summary_digest(first),
                self.helper.summary_digest(second),
            )
            self.assertNotEqual(
                self.helper.summary_digest(first),
                self.helper.summary_digest(repository),
            )

    def test_publish_dispatches_current_branch_head_when_baseline_is_missing(self):
        with mock.patch.object(
            self.helper, "get_target_branch_head_sha", return_value=BASE_SHA
        ), mock.patch.object(
            self.helper, "read_remote_baseline", return_value=None
        ), mock.patch.object(
            self.helper, "find_active_run", return_value=None
        ), mock.patch.object(self.helper.subprocess, "run") as subprocess_run:
            result = self.helper.publish_missing_baseline(TARGET_BRANCH_NAME)

        self.assertIsNone(result)
        subprocess_run.assert_called_once_with(
            [
                "gh",
                "workflow",
                "run",
                "pek-ci.yml",
                "--ref",
                "develop",
                "-f",
                "checks=valgrind",
                "-f",
                f"target_branch_head_sha={BASE_SHA}",
                "-f",
                f"target_branch_name={TARGET_BRANCH_NAME}",
            ],
            check=True,
        )

    def test_publish_replaces_an_outdated_branch_baseline(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            content = self.summary(Path(tmpdir) / "summary.xml")
            with mock.patch.object(
                self.helper, "get_target_branch_head_sha", return_value=BASE_SHA
            ), mock.patch.object(
                self.helper, "read_remote_baseline", return_value=(OLD_SHA, content)
            ), mock.patch.object(
                self.helper, "find_active_run", return_value=None
            ), mock.patch.object(self.helper.subprocess, "run") as subprocess_run:
                result = self.helper.publish_missing_baseline(TARGET_BRANCH_NAME)

        self.assertIsNone(result)
        subprocess_run.assert_called_once()

    def test_publish_skips_the_current_branch_baseline(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            content = self.summary(Path(tmpdir) / "summary.xml")
            with mock.patch.object(
                self.helper, "get_target_branch_head_sha", return_value=BASE_SHA
            ), mock.patch.object(
                self.helper, "read_remote_baseline", return_value=(BASE_SHA, content)
            ), mock.patch.object(self.helper, "find_active_run") as find_active_run:
                result = self.helper.publish_missing_baseline(TARGET_BRANCH_NAME)

        self.assertIsNone(result)
        find_active_run.assert_not_called()

    def test_find_active_run_matches_branch_and_sha_in_the_run_name(self):
        with mock.patch.object(
            self.helper,
            "list_backfill_runs",
            return_value=[
                {
                    "databaseId": 123,
                    "displayTitle": f"Valgrind baseline {TARGET_BRANCH_NAME}@{BASE_SHA}",
                    "status": "in_progress",
                },
                {
                    "databaseId": 456,
                    "displayTitle": f"Valgrind baseline main@{BASE_SHA}",
                    "status": "in_progress",
                },
            ],
        ):
            self.assertEqual(self.helper.find_active_run(TARGET_BRANCH_NAME, BASE_SHA), 123)

    def test_main_reads_target_branch_name_and_head_from_the_environment(self):
        with tempfile.TemporaryDirectory() as tmpdir, mock.patch.dict(
            os.environ,
            {
                "TARGET_BRANCH_NAME": TARGET_BRANCH_NAME,
                "TARGET_BRANCH_HEAD_SHA": BASE_SHA,
            },
        ), mock.patch.object(
            self.helper.sys,
            "argv",
            ["valgrind-baseline-artifact.py", "wait", "--output-dir", tmpdir],
        ), mock.patch.object(
            self.helper, "wait_for_baseline", return_value=0
        ) as wait:
            self.assertEqual(self.helper.main(), 0)

        wait.assert_called_once_with(TARGET_BRANCH_NAME, BASE_SHA, Path(tmpdir))

    def test_main_runs_the_publish_command(self):
        with mock.patch.dict(
            os.environ,
            {"TARGET_BRANCH_NAME": TARGET_BRANCH_NAME},
        ), mock.patch.object(
            self.helper.sys,
            "argv",
            ["valgrind-baseline-artifact.py", "publish"],
        ), mock.patch.object(self.helper, "publish_missing_baseline") as publish:
            self.assertEqual(self.helper.main(), 0)

        publish.assert_called_once_with(TARGET_BRANCH_NAME)

    def test_wait_downloads_the_published_baseline(self):
        with tempfile.TemporaryDirectory() as tmpdir, mock.patch.object(
            self.helper, "download_baseline", side_effect=[1, 0]
        ) as download, mock.patch.object(self.helper.time, "sleep"):
            code = self.helper.wait_for_baseline(TARGET_BRANCH_NAME, BASE_SHA, Path(tmpdir))

        self.assertEqual(code, 0)
        self.assertEqual(download.call_count, 2)


if __name__ == "__main__":
    unittest.main()
