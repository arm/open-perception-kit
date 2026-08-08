#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import importlib.util
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT_PATH = Path(__file__).with_name("valgrind-baseline-artifact.py")
BASE_SHA = "a" * 40


def load_helper():
    spec = importlib.util.spec_from_file_location("valgrind_baseline_artifact", SCRIPT_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    with mock.patch.dict(os.environ, {"GITHUB_REPOSITORY": "example/repo"}):
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

    def test_download_requires_one_exact_sha_tag_and_verifies_content(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            output = Path(tmpdir)
            expected = output / self.helper.SUMMARY_NAME
            content = self.summary(output / "source.xml")
            digest = self.helper.summary_digest(output / "source.xml")
            tag = f"v2-sha-{BASE_SHA}-{digest}"

            def docker(*args, **_kwargs):
                if args[0] == "create":
                    return "container-id"
                if args[0] == "cp":
                    Path(args[2]).write_bytes(content)
                return ""

            with mock.patch.object(self.helper, "gh", return_value=tag), \
                    mock.patch.object(self.helper, "docker", side_effect=docker) as run:
                code = self.helper.download_baseline(BASE_SHA, output)

            self.assertEqual(code, 0)
            self.assertEqual(expected.read_bytes(), content)
            self.assertEqual(run.call_args_list[-1], mock.call("rm", "-f", "container-id"))

    def test_ambiguous_sha_does_not_pull(self):
        tags = "\n".join(
            [
                f"v2-sha-{BASE_SHA}-{'1' * 64}",
                f"v2-sha-{BASE_SHA}-{'2' * 64}",
            ]
        )
        with tempfile.TemporaryDirectory() as tmpdir, \
                mock.patch.object(self.helper, "gh", return_value=tags), \
                mock.patch.object(self.helper, "docker") as docker:
            code = self.helper.download_baseline(BASE_SHA, Path(tmpdir))

        self.assertEqual(code, 1)
        docker.assert_not_called()

    def test_upload_skips_existing_identical_baseline(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            summary = Path(tmpdir) / self.helper.SUMMARY_NAME
            self.summary(summary)
            tag = f"v2-sha-{BASE_SHA}-{self.helper.summary_digest(summary)}"
            with mock.patch.object(self.helper, "gh", return_value=tag), \
                    mock.patch.object(self.helper, "docker") as docker:
                code = self.helper.upload_baseline(BASE_SHA, summary)

        self.assertEqual(code, 0)
        docker.assert_not_called()

    def test_upload_rejects_conflicting_baseline(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            summary = Path(tmpdir) / self.helper.SUMMARY_NAME
            self.summary(summary)
            with mock.patch.object(
                self.helper,
                "gh",
                return_value=f"v2-sha-{BASE_SHA}-{'1' * 64}",
            ):
                with self.assertRaisesRegex(RuntimeError, "Conflicting"):
                    self.helper.upload_baseline(BASE_SHA, summary)

    def test_upload_retries_transient_registry_failure(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            summary = Path(tmpdir) / self.helper.SUMMARY_NAME
            self.summary(summary)
            pushes = 0

            def docker(*args, **_kwargs):
                nonlocal pushes
                if args[0] == "push":
                    pushes += 1
                    if pushes == 1:
                        raise subprocess.CalledProcessError(1, args)
                return ""

            with mock.patch.object(self.helper, "gh", return_value=""), \
                    mock.patch.object(self.helper, "docker", side_effect=docker), \
                    mock.patch.object(self.helper.time, "sleep"):
                code = self.helper.upload_baseline(BASE_SHA, summary)

        self.assertEqual(code, 0)
        self.assertEqual(pushes, 2)

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

    def test_publish_dispatches_only_when_baseline_and_run_are_missing(self):
        with mock.patch.object(self.helper, "baseline_sha", return_value=BASE_SHA), \
                mock.patch.object(self.helper, "commit_tags", return_value=[]), \
                mock.patch.object(self.helper, "find_active_run", return_value=None), \
                mock.patch.object(self.helper.subprocess, "run") as subprocess_run:
            code = self.helper.publish_missing_baseline()

        self.assertEqual(code, 0)
        subprocess_run.assert_called_once_with(
            [
                "gh", "workflow", "run", "pek-ci.yml",
                "--ref", "develop",
                "-f", "checks=valgrind",
                "-f", f"valgrind_baseline_sha={BASE_SHA}",
            ],
            check=True,
        )

    def test_wait_downloads_the_published_baseline(self):
        with tempfile.TemporaryDirectory() as tmpdir, \
                mock.patch.object(self.helper, "download_baseline", side_effect=[1, 0]) as download, \
                mock.patch.object(self.helper.time, "sleep"):
            code = self.helper.wait_for_baseline(BASE_SHA, Path(tmpdir))

        self.assertEqual(code, 0)
        self.assertEqual(download.call_count, 2)


if __name__ == "__main__":
    unittest.main()
