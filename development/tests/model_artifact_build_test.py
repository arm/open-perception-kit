#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DOWNLOAD_SCRIPT = REPO_ROOT / "scripts" / "download-models.py"


class ModelArtifactBuildTest(unittest.TestCase):
    def test_model_artifacts_are_ignored_except_checked_in_models(self) -> None:
        expected = [
            "config/models/**/*.hef",
            "config/models/**/*.onnx",
            "config/models/**/*.pte",
            "!config/models/paddleocr/classification.onnx",
            "!config/models/paddleocr/recognition.onnx",
            "!config/models/yolov11/yolo11n-fp32-320.onnx",
            "!config/models/yolox/yolox_nano.pte",
        ]
        for ignore_file in (".dockerignore", ".gitignore"):
            rules = [
                line
                for line in (REPO_ROOT / ignore_file).read_text().splitlines()
                if line.startswith(("config/models/", "!config/models/"))
            ]
            self.assertEqual(rules, expected)

    def test_dev_seed_removes_stale_skipped_download(self) -> None:
        entrypoint = (
            REPO_ROOT / "scripts/private/development-entrypoint.sh"
        ).read_text()
        seed_script = entrypoint.partition("<< 'PY'\n")[2].partition("\nPY\n")[0]

        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            source = root / "source"
            destination = root / "destination"
            descriptor = source / "restricted/model.json"
            stale_artifact = destination / "restricted/model.onnx"
            descriptor.parent.mkdir(parents=True)
            stale_artifact.parent.mkdir(parents=True)
            descriptor.write_text(
                json.dumps({"modelFile": "model.onnx", "hfDownload": {}})
            )
            stale_artifact.write_text("stale")

            subprocess.run(
                [sys.executable, "-", str(source), str(destination)],
                input=seed_script,
                check=True,
                text=True,
            )

            self.assertFalse(stale_artifact.exists())

    def test_failed_download_is_skipped_and_model_file_names_the_output(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            scripts = root / "scripts"
            fake_hub = root / "pythonpath" / "huggingface_hub"
            cache = root / "cache"
            scripts.mkdir()
            fake_hub.mkdir(parents=True)
            cache.mkdir()
            shutil.copy2(DOWNLOAD_SCRIPT, scripts / DOWNLOAD_SCRIPT.name)

            for name, model_file, hub_file in (
                ("first", "missing.onnx", "missing.onnx"),
                ("second", "renamed.hef", "available.onnx"),
            ):
                model_dir = root / "config" / "models" / name
                model_dir.mkdir(parents=True)
                (model_dir / "model.json").write_text(
                    json.dumps(
                        {
                            "modelFile": model_file,
                            "hfDownload": {
                                "repo_id": "test/repo",
                                "revision": "revision",
                                "filename": hub_file,
                            },
                        }
                    )
                )

            (fake_hub / "__init__.py").write_text("""import os
from pathlib import Path


def hf_hub_download(*, repo_id, revision, filename, token):
    Path(os.environ["HF_TOKEN_CAPTURE"]).write_text(repr(token))
    if filename == "missing.onnx":
        raise RuntimeError("download failed")
    downloaded = Path(os.environ["HF_FAKE_CACHE"]) / filename
    downloaded.write_text("model")
    return downloaded
""")

            environment = dict(os.environ) | {
                "HF_FAKE_CACHE": str(cache),
                "HF_TOKEN": "must-be-ignored-without-token-env",
                "HF_TOKEN_CAPTURE": str(root / "captured-token"),
                "PYTHONPATH": str(fake_hub.parent),
            }
            result = subprocess.run(
                [
                    sys.executable,
                    str(scripts / DOWNLOAD_SCRIPT.name),
                    "--models-dir",
                    "config/models",
                ],
                check=True,
                cwd=root,
                env=environment,
                capture_output=True,
                text=True,
            )

            self.assertFalse((root / "config/models/first/missing.onnx").exists())
            self.assertEqual(
                (root / "config/models/second/renamed.hef").read_text(),
                "model",
            )
            self.assertIn(
                "No Hugging Face token supplied; downloading public models "
                "anonymously.",
                result.stderr,
            )
            self.assertIn(
                "Downloading test/repo/missing.onnx to "
                "config/models/first/missing.onnx.",
                result.stderr,
            )
            self.assertIn(
                "Skipping config/models/first/missing.onnx: download failed",
                result.stderr,
            )
            self.assertIn(
                "WARNING: available.onnx uses .onnx, but "
                "config/models/second/renamed.hef uses .hef; saving as configured.",
                result.stderr,
            )
            self.assertEqual((root / "captured-token").read_text(), "False")

            environment["MODEL_DOWNLOAD_TOKEN"] = "test-token"
            subprocess.run(
                [
                    sys.executable,
                    str(scripts / DOWNLOAD_SCRIPT.name),
                    "--models-dir",
                    "config/models",
                    "--token-env",
                    "MODEL_DOWNLOAD_TOKEN",
                ],
                check=True,
                cwd=root,
                env=environment,
                capture_output=True,
                text=True,
            )
            self.assertEqual((root / "captured-token").read_text(), "'test-token'")

            help_result = subprocess.run(
                [sys.executable, str(scripts / DOWNLOAD_SCRIPT.name), "--help"],
                check=True,
                env=environment,
                capture_output=True,
                text=True,
            )
            self.assertIn("--models-dir MODELS_DIR", help_result.stdout)
            self.assertIn("--token-env NAME", help_result.stdout)

            invalid_result = subprocess.run(
                [
                    sys.executable,
                    str(scripts / DOWNLOAD_SCRIPT.name),
                    "--models-dir",
                    str(root / "missing-models"),
                ],
                env=environment,
                capture_output=True,
                text=True,
            )
            self.assertEqual(invalid_result.returncode, 2)
            self.assertIn("--models-dir is not a directory", invalid_result.stderr)


if __name__ == "__main__":
    unittest.main()
