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


if __name__ == "__main__":
    unittest.main()
