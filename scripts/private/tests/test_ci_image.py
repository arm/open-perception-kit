################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import importlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

ci_image = importlib.import_module("ci_image")


SHA = "1" * 40
IMAGE = f"pek-ci:{SHA}"
HEAD_SHA = "2" * 40
BASE_SHA = "3" * 40
COMPAT_SHA = "4" * 40


class CiImageTests(unittest.TestCase):
    def test_image_ref_is_sha_pinned_and_lowercase(self):
        self.assertEqual(ci_image.image_ref(SHA), IMAGE)

    def test_prepare_loads_verifies_and_tags_the_image(self):
        command_result: subprocess.CompletedProcess[str] = subprocess.CompletedProcess([], 0)
        with (
            tempfile.TemporaryDirectory() as tmpdir,
            mock.patch.dict(
                "os.environ",
                {
                    "COMPOSE_PROJECT_NAME": "pek-test",
                    "GITHUB_ENV": str(Path(tmpdir) / "github-env"),
                },
                clear=True,
            ),
            mock.patch.object(ci_image, "run", return_value=command_result) as run,
            mock.patch.object(ci_image, "verify_revision") as verify,
        ):
            archive = Path(tmpdir) / "pek-ci-image.tar"
            archive.touch()
            ci_image.prepare(SHA, str(archive), ["pek-sonar-check", "pek-valgrind-check"])
            self.assertFalse(archive.exists())
            self.assertEqual(
                (Path(tmpdir) / "github-env").read_text(encoding="utf-8").splitlines(),
                ["COMPOSE_PROJECT_NAME=pek-test", f"PEK_CI_IMAGE={IMAGE}"],
            )

        verify.assert_called_once_with(IMAGE, SHA)
        self.assertEqual(
            run.call_args_list,
            [
                mock.call(["docker", "image", "load", "--input", str(archive)]),
                mock.call(["docker", "tag", IMAGE, "pek-test-pek-sonar-check"]),
                mock.call(["docker", "tag", IMAGE, "pek-test-pek-valgrind-check"]),
            ],
        )

    def test_prepare_dev_uses_compatible_base_image(self):
        command_result: subprocess.CompletedProcess[str] = subprocess.CompletedProcess([], 0)
        base_image = f"ghcr.io/arm-debug/amp-dev-forge-dev:sha-{BASE_SHA}"
        with (
            tempfile.TemporaryDirectory() as tmpdir,
            mock.patch.dict(
                "os.environ",
                {
                    "COMPOSE_PROJECT_NAME": "pek-test",
                    "GITHUB_OUTPUT": str(Path(tmpdir) / "github-output"),
                    "GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge",
                },
                clear=True,
            ),
            mock.patch.object(ci_image, "git_head_sha", return_value=HEAD_SHA),
            mock.patch.object(ci_image, "compatible_dev_sha", return_value=COMPAT_SHA),
            mock.patch.object(ci_image, "pull_dev_image", side_effect=[None, base_image]) as pull,
            mock.patch.object(ci_image, "dev_inputs_unchanged", return_value=True),
            mock.patch.object(ci_image, "run", return_value=command_result) as run,
        ):
            self.assertEqual(ci_image.prepare_dev(BASE_SHA), base_image)
            self.assertEqual(
                (Path(tmpdir) / "github-output").read_text(encoding="utf-8").splitlines(),
                ["start_args=--no-build"],
            )

        self.assertEqual(pull.call_args_list, [mock.call(HEAD_SHA), mock.call(BASE_SHA)])
        run.assert_called_once_with(["docker", "tag", base_image, "pek-test-pek-dev"])

    def test_retention_deletes_untagged_and_old_sha_only_versions(self):
        versions = [
            {
                "id": 1,
                "created_at": "2026-01-04",
                "metadata": {"container": {"tags": [f"sha-{HEAD_SHA}"]}},
            },
            {"id": 2, "created_at": "2026-01-03", "metadata": {"container": {"tags": ["buildcache"]}}},
            {
                "id": 3,
                "created_at": "2026-01-02",
                "metadata": {"container": {"tags": [f"sha-{BASE_SHA}"]}},
            },
            {
                "id": 4,
                "created_at": "2026-01-01",
                "metadata": {"container": {"tags": ["sha-protected"]}},
            },
            {"id": 5, "created_at": "2026-01-05", "metadata": {"container": {"tags": []}}},
        ]
        self.assertEqual(ci_image.versions_to_delete(versions, keep=1), [3, 5])


if __name__ == "__main__":
    unittest.main()
