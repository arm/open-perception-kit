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
OLD_SHA = "2" * 40
REPOSITORY_ENV = {"GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge"}
IMAGE = f"ghcr.io/arm-debug/amp-dev-forge-ci:{SHA}"
RUN_TAG = f"{SHA}-123-2"
RUN_IMAGE = f"ghcr.io/arm-debug/amp-dev-forge-ci:{RUN_TAG}"


def version(version_id: int, *tags: str) -> dict[str, object]:
    return {"id": version_id, "metadata": {"container": {"tags": list(tags)}}}


class CiImageTests(unittest.TestCase):
    def test_image_ref_is_sha_pinned_and_lowercase(self):
        with mock.patch.dict("os.environ", REPOSITORY_ENV, clear=True):
            self.assertEqual(
                ci_image.image_ref(SHA),
                f"ghcr.io/arm-debug/amp-dev-forge-ci:{SHA}",
            )

    def test_image_ref_accepts_a_run_scoped_non_pr_tag(self):
        with mock.patch.dict("os.environ", REPOSITORY_ENV, clear=True):
            self.assertEqual(
                ci_image.image_ref(RUN_TAG),
                RUN_IMAGE,
            )

    def test_prepare_pulls_then_tags_each_compose_service(self):
        command_result: subprocess.CompletedProcess[str] = subprocess.CompletedProcess(
            [], 0
        )
        with (
            tempfile.NamedTemporaryFile() as env_file,
            mock.patch.dict(
                "os.environ",
                {
                    **REPOSITORY_ENV,
                    "COMPOSE_PROJECT_NAME": "pek-test",
                    "GITHUB_ENV": env_file.name,
                },
                clear=True,
            ),
            mock.patch.object(
                ci_image,
                "run",
                side_effect=[command_result, command_result, command_result],
            ) as run,
            mock.patch.object(ci_image, "verify_revision") as verify,
        ):
            ci_image.prepare(SHA, ["pek-sonar-check", "pek-valgrind-check"])

        verify.assert_called_once_with(IMAGE, SHA)
        self.assertEqual(run.call_args_list[0], mock.call(["docker", "pull", IMAGE]))
        self.assertEqual(
            run.call_args_list[-2:],
            [
                mock.call(["docker", "tag", IMAGE, "pek-test-pek-sonar-check"]),
                mock.call(["docker", "tag", IMAGE, "pek-test-pek-valgrind-check"]),
            ],
        )

    def test_cleanup_deletes_only_the_matching_version(self):
        versions = [version(10, SHA), version(20, OLD_SHA), version(30, ci_image.ANCHOR_TAG)]
        with (
            mock.patch.dict("os.environ", REPOSITORY_ENV, clear=True),
            mock.patch.object(ci_image, "package_versions", side_effect=[versions, versions[1:]]),
            mock.patch.object(ci_image, "github_api_request") as request,
        ):
            ci_image.cleanup(SHA)

        request.assert_called_once_with(
            "orgs/Arm-Debug/packages/container/amp-dev-forge-ci/versions/10",
            method="DELETE",
        )

    def test_cleanup_refuses_package_wide_delete_without_the_anchor(self):
        versions = [version(10, SHA)]
        with (
            mock.patch.dict("os.environ", REPOSITORY_ENV, clear=True),
            mock.patch.object(ci_image, "package_versions", return_value=versions),
            mock.patch.object(ci_image, "github_api_request") as request,
        ):
            with self.assertRaisesRegex(RuntimeError, "retention anchor is missing"):
                ci_image.cleanup(SHA)

        request.assert_not_called()

    def test_cleanup_refuses_to_delete_a_version_with_other_tags(self):
        versions = [version(10, SHA, RUN_TAG), version(30, ci_image.ANCHOR_TAG)]
        with (
            mock.patch.dict("os.environ", REPOSITORY_ENV, clear=True),
            mock.patch.object(ci_image, "package_versions", return_value=versions),
            mock.patch.object(ci_image, "github_api_request") as request,
        ):
            with self.assertRaisesRegex(RuntimeError, "also contains other tags"):
                ci_image.cleanup(SHA)

        request.assert_not_called()

    def test_cleanup_is_idempotent_when_tag_is_missing(self):
        with (
            mock.patch.dict("os.environ", REPOSITORY_ENV, clear=True),
            mock.patch.object(ci_image, "package_versions", return_value=[version(20, OLD_SHA)]),
            mock.patch.object(ci_image, "github_api_request") as request,
        ):
            ci_image.cleanup(SHA)

        request.assert_not_called()

    def test_cleanup_metadata_uses_the_recorded_run_tag(self):
        with tempfile.NamedTemporaryFile(mode="w+", encoding="utf-8") as metadata_file:
            ci_image.write_metadata(RUN_TAG, "false", metadata_file.name)
            with mock.patch.object(ci_image, "cleanup") as cleanup:
                ci_image.cleanup_metadata(metadata_file.name)

        cleanup.assert_called_once_with(RUN_TAG)

    def test_cleanup_metadata_keeps_pr_images_for_pr_lifecycle_cleanup(self):
        with tempfile.NamedTemporaryFile(mode="w+", encoding="utf-8") as metadata_file:
            ci_image.write_metadata(SHA, "true", metadata_file.name)
            with mock.patch.object(ci_image, "cleanup") as cleanup:
                ci_image.cleanup_metadata(metadata_file.name)

        cleanup.assert_not_called()


if __name__ == "__main__":
    unittest.main()
