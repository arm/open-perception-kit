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
MODELS_DIR = "config/models"


class ModelArtifactBuildTest(unittest.TestCase):
    def test_tokenless_defaults_use_bundled_yolov11(self) -> None:
        pipeline = json.loads(
            (REPO_ROOT / "config/pipelines/yolov11-onnx.json").read_text()
        )
        inference_steps = [
            step for step in pipeline["pipeline"] if "pekinfer" in step
        ]
        self.assertEqual(
            inference_steps,
            [
                "pekinfer opchain-path=/work/config/models/yolov11/opchain.json "
                "active=true !"
            ],
        )
        self.assertIn(
            "PEK_MENU_ARGS=(yolov11-onnx)",
            (REPO_ROOT / "scripts/run.sh").read_text(),
        )
        self.assertIn(
            "PEK_PIPELINE: ${PEK_PIPELINE:-yolov11-onnx}",
            (REPO_ROOT / "compose.yaml").read_text(),
        )
        self.assertIn(
            "ARG PEK_PIPELINE=yolov11-onnx",
            (REPO_ROOT / "Dockerfile").read_text(),
        )
        for script in (
            "scripts/private/deployment-process.sh",
            "scripts/private/deployment-runtime.sh",
        ):
            self.assertIn(
                'PEK_PIPELINE=${PEK_PIPELINE:-"yolov11-onnx"}',
                (REPO_ROOT / script).read_text(),
            )
        self.assertTrue(
            (
                REPO_ROOT
                / "config/models/yolov11/yolo11n-fp32-320.onnx"
            ).is_file()
        )

    def test_dev_container_seeds_downloaded_artifacts(self) -> None:
        dockerfile = (REPO_ROOT / "Dockerfile").read_text()
        entrypoint = (
            REPO_ROOT / "scripts/private/development-entrypoint.sh"
        ).read_text()
        runtime_stage = dockerfile.split(" AS pek-dev-base", 1)[1].split(
            "FROM pek-dev-base AS pek-dev-tools", 1
        )[0]
        self.assertIn("huggingface_hub==1.18.0", runtime_stage)
        self.assertIn(
            "COPY --from=pek-models \\\n"
            "  /work/config/models /opt/pek-app/config/models",
            dockerfile,
        )
        self.assertIn(
            'cp -R --no-clobber "${artifacts_root}/config/models/." '
            "/work/config/models/",
            entrypoint,
        )
        self.assertIn(
            "COPY --from=pek-demo-media \\\n"
            "  /work/data/videos /opt/pek-app/data/videos",
            dockerfile,
        )
        self.assertIn(
            'cp -a --no-clobber "${artifacts_root}/data/videos/." '
            "/work/data/videos/",
            entrypoint,
        )

    def test_model_download_cache_bust_is_consumed(self) -> None:
        for name in ("Dockerfile",):
            dockerfile = (REPO_ROOT / name).read_text()
            download_step = dockerfile.split("ARG HF_DOWNLOAD_CACHEBUST", 1)[1].split(
                "\n\n", 1
            )[0]
            self.assertIn(
                'HF_DOWNLOAD_CACHEBUST="${HF_DOWNLOAD_CACHEBUST}"',
                download_step,
            )
            self.assertIn(
                "--mount=type=cache,target=/root/.cache/huggingface",
                download_step,
            )

        cache_bust = (
            "${HF_DOWNLOAD_CACHEBUST:-${HF_TOKEN:+${GITHUB_RUN_ID:?Set "
            "HF_DOWNLOAD_CACHEBUST when HF_TOKEN is set}-"
            "${GITHUB_RUN_ATTEMPT:-0}}}"
        )
        for name, count in (
            ("compose.yaml", 1),
            (".devcontainer/compose.devcont.yaml", 1),
            (".github/compose.ci.yaml", 2),
        ):
            self.assertEqual(
                (REPO_ROOT / name).read_text().count(
                    f"HF_DOWNLOAD_CACHEBUST: {cache_bust}"
                ),
                count,
            )

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

    def test_release_workflows_resolve_models_once(self) -> None:
        for workflow_name in ("release-tests.yml", "release-packages.yml"):
            workflow = (
                REPO_ROOT / ".github/workflows" / workflow_name
            ).read_text()
            self.assertEqual(workflow.count("Resolve pinned model artifacts"), 1)
            self.assertEqual(workflow.count("Upload resolved models"), 1)
            self.assertEqual(workflow.count("Download resolved models"), 2)
            self.assertIn("HF_TOKEN: ${{ secrets.HF_TOKEN }}", workflow)
            self.assertIn(
                "scripts/download-models.py \\\n"
                '            --models-dir config/models --token "$HF_TOKEN"',
                workflow,
            )

    def test_manual_release_accepts_selected_source(self) -> None:
        workflow = (
            REPO_ROOT / ".github/workflows/release-packages.yml"
        ).read_text()
        self.assertNotIn(
            "if: github.event_name == 'push' || github.ref == 'refs/heads/main'",
            workflow,
        )
        self.assertLess(
            workflow.index("Checkout release tooling"),
            workflow.index("Checkout selected source"),
        )
        self.assertIn("path: source", workflow)
        self.assertIn(
            "python3 scripts/release/ReleaseTool.py prepare \\\n"
            "            --repo-root source",
            workflow,
        )
        artifactory = workflow.split("\n  artifactory:\n", 1)[1]
        self.assertIn(
            "needs: [prepare, smoke-x86, smoke-arm, build-docs, "
            "github-release]",
            artifactory,
        )
        for dependency in ("prepare", "smoke-x86", "smoke-arm", "build-docs"):
            self.assertIn(
                f"needs.{dependency}.result == 'success'", artifactory
            )
        self.assertIn("github.event_name == 'workflow_dispatch'", artifactory)
        self.assertIn(
            "needs.github-release.result == 'success'", artifactory
        )
        self.assertNotIn("needs.prepare.outputs.x86_archive", artifactory)
        self.assertIn(
            "BUILD_ID: ${{ needs.prepare.outputs.build_id }}", artifactory
        )
        self.assertNotIn("=~", artifactory)

    def test_download_cli_contract(self) -> None:
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


def hf_hub_download(*, repo_id, revision, filename, token, cache_dir):
    Path(os.environ["HF_TOKEN_CAPTURE"]).write_text(
        f"{token!r}\\n{cache_dir}"
    )
    if filename == "missing.onnx":
        raise RuntimeError("download failed")
    downloaded = Path(os.environ["HF_FAKE_CACHE"]) / filename
    downloaded.write_text("model")
    return downloaded
""")
            (fake_hub / "constants.py").write_text("""import os
from pathlib import Path


HF_HUB_CACHE = Path(os.environ["HF_HOME"]) / "hub"
""")

            environment = dict(os.environ) | {
                "HF_FAKE_CACHE": str(cache),
                "HF_HOME": str(root / "hub-cache"),
                "HF_TOKEN": "must-be-ignored-without-token-env",
                "HF_TOKEN_CAPTURE": str(root / "captured-token"),
                "PYTHONPATH": str(fake_hub.parent),
            }
            result = subprocess.run(
                [
                    sys.executable,
                    str(scripts / DOWNLOAD_SCRIPT.name),
                    "--models-dir",
                    MODELS_DIR,
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
            anonymous_capture = (root / "captured-token").read_text().splitlines()
            self.assertEqual(
                anonymous_capture,
                ["False", str(root / "hub-cache/hub/anonymous")],
            )

            environment["HF_TOKEN"] = ""
            subprocess.run(
                [
                    sys.executable,
                    str(scripts / DOWNLOAD_SCRIPT.name),
                    "--models-dir",
                    MODELS_DIR,
                    "--token",
                ],
                check=True,
                cwd=root,
                env=environment,
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                (root / "captured-token").read_text().splitlines(),
                ["False", str(root / "hub-cache/hub/anonymous")],
            )

            subprocess.run(
                [
                    sys.executable,
                    str(scripts / DOWNLOAD_SCRIPT.name),
                    "--models-dir",
                    MODELS_DIR,
                    "--token",
                    "test-token",
                ],
                check=True,
                cwd=root,
                env=environment,
                capture_output=True,
                text=True,
            )
            first_token_capture = (
                root / "captured-token"
            ).read_text().splitlines()
            first_token_cache = Path(first_token_capture[1])
            self.assertEqual(first_token_capture[0], "'test-token'")
            self.assertEqual(first_token_cache.parent, root / "hub-cache/hub")
            self.assertNotEqual(first_token_cache, Path(anonymous_capture[1]))
            self.assertNotIn("test-token", first_token_cache.name)

            subprocess.run(
                [
                    sys.executable,
                    str(scripts / DOWNLOAD_SCRIPT.name),
                    "--models-dir",
                    MODELS_DIR,
                    "--token",
                    "lower-access-token",
                ],
                check=True,
                cwd=root,
                env=environment,
                capture_output=True,
                text=True,
            )
            second_token_capture = (
                root / "captured-token"
            ).read_text().splitlines()
            second_token_cache = Path(second_token_capture[1])
            self.assertEqual(second_token_capture[0], "'lower-access-token'")
            self.assertEqual(second_token_cache.parent, root / "hub-cache/hub")
            self.assertNotEqual(second_token_cache, first_token_cache)
            self.assertNotIn("lower-access-token", second_token_cache.name)

            help_result = subprocess.run(
                [sys.executable, str(scripts / DOWNLOAD_SCRIPT.name), "--help"],
                check=True,
                env=environment,
                capture_output=True,
                text=True,
            )
            self.assertIn("--models-dir MODELS_DIR", help_result.stdout)
            self.assertIn("--token [TOKEN]", help_result.stdout)

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
