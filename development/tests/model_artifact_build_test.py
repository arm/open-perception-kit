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
COMPOSE_FILE = "compose.yaml"
DOCKER_UNAVAILABLE = "Docker CLI is not installed"
MODELS_DIR = "config/models"
MODEL_DESCRIPTOR = "model.json"
SCHEMAS_DIR = Path("config/schemas")
MODEL_SCHEMA = SCHEMAS_DIR / "v1/model.schema.json"


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
                'pekinfer opchain-path="${PEK_PROJECT_ROOT:-/work}/config/models/'
                'yolov11/opchain.json" active=true !'
            ],
        )
        self.assertIn(
            "PEK_MENU_ARGS=(yolov11-onnx)",
            (REPO_ROOT / "scripts/run.sh").read_text(),
        )
        self.assertIn(
            "PEK_PIPELINE: ${PEK_PIPELINE:-yolov11-onnx}",
            (REPO_ROOT / COMPOSE_FILE).read_text(),
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
        self.assertEqual(dockerfile.count("jsonschema==4.26.0"), 2)
        self.assertIn(
            "COPY --from=pek-models \\\n"
            "  /work/config/models /opt/pek-app/config/models",
            dockerfile,
        )
        self.assertIn(
            '"${PEK_PROJECT_ROOT}/config/models/"',
            entrypoint,
        )
        self.assertNotIn("/work", entrypoint)
        self.assertNotIn(
            "/work", (REPO_ROOT / ".devcontainer/setup.sh").read_text()
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
                'if [ -z "${HF_DOWNLOAD_CACHEBUST}" ]',
                download_step,
            )
            self.assertIn(
                "--mount=type=cache,target=/root/.cache/huggingface",
                download_step,
            )

        self.assertEqual(
            (REPO_ROOT / "compose.base.yaml").read_text().count(
                "HF_DOWNLOAD_CACHEBUST: ${HF_DOWNLOAD_CACHEBUST:-}"
            ),
            1,
        )
        for name, service in (
            (COMPOSE_FILE, "pek-model-image"),
            (".devcontainer/compose.devcont.yaml", "pek-common-dev-model-image"),
            (".github/compose.ci.yaml", "pek-model-image"),
            (".github/compose.ci.yaml", "pek-common-dev-model-image"),
        ):
            self.assertIn(
                f"service: {service}",
                (REPO_ROOT / name).read_text(),
            )
        for name in (
            ".devcontainer/platform_init.sh",
            "scripts/quick-start/start-container.sh",
            "scripts/private/run-console.sh",
        ):
            self.assertIn(
                "scripts/private/generate-hf-download-cachebust.sh",
                (REPO_ROOT / name).read_text(),
            )
        workflow_step = (
            "      - name: Generate Hugging Face download cache key\n"
            "        working-directory: ${{ github.workspace }}/"
            "${{ env.CI_CHECKOUT_PATH }}\n"
            "        run: |\n"
            "          set -euo pipefail\n"
            "          cache_key=\"$(scripts/private/"
            "generate-hf-download-cachebust.sh)\"\n"
            "          echo \"HF_DOWNLOAD_CACHEBUST=${cache_key}\" "
            ">> \"$GITHUB_ENV\""
        )
        docker_scout = REPO_ROOT / ".github/workflows/docker-scout-image-audit.yml"
        self.assertIn(workflow_step, docker_scout.read_text())

        pek_ci = (REPO_ROOT / ".github/workflows/pek-ci.yml").read_text()
        self.assertIn(
            "      - name: Validate model cache key guard\n"
            "        env:\n"
            '          PEK_REQUIRE_DOCKER_BUILD_TEST: "1"\n'
            "        run: >-\n"
            "          python3 development/tests/model_artifact_build_test.py\n"
            "          ModelArtifactBuildTest."
            "test_raw_model_build_requires_cache_key\n"
            "          ModelArtifactBuildTest."
            "test_main_compose_uses_model_bearing_target",
            pek_ci,
        )
        self.assertIn(
            "env -u HF_TOKEN -u HF_DOWNLOAD_CACHEBUST docker compose "
            "-f compose.yaml config --quiet",
            pek_ci,
        )
        self.assertIn(
            "      - name: Generate Hugging Face download cache key\n"
            "        run: |\n"
            "          set -euo pipefail\n"
            "          cache_key=\"$(scripts/private/"
            "generate-hf-download-cachebust.sh)\"\n"
            "          echo \"HF_DOWNLOAD_CACHEBUST=${cache_key}\" "
            ">> \"$GITHUB_ENV\"",
            pek_ci,
        )

    def test_tokenless_compose_config(self) -> None:
        docker = shutil.which("docker")
        if docker is None:
            self.skipTest(DOCKER_UNAVAILABLE)
        if subprocess.run(
            [docker, "compose", "version"],
            capture_output=True,
            check=False,
        ).returncode:
            self.skipTest("Docker Compose is not installed")

        env = os.environ.copy()
        env.pop("HF_TOKEN", None)
        env.pop("HF_DOWNLOAD_CACHEBUST", None)
        config = subprocess.run(
            [docker, "compose", "-f", COMPOSE_FILE, "config", "--format", "json"],
            cwd=REPO_ROOT,
            env=env,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(config.returncode, 0, config.stderr)
        build_args = json.loads(config.stdout)["services"]["pek-dev"]["build"][
            "args"
        ]
        self.assertEqual(build_args["HF_DOWNLOAD_CACHEBUST"], "")

        env["HF_TOKEN"] = "test-token"
        authenticated_config = subprocess.run(
            [docker, "compose", "-f", COMPOSE_FILE, "config", "--format", "json"],
            cwd=REPO_ROOT,
            env=env,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(authenticated_config.returncode, 0, authenticated_config.stderr)
        authenticated_args = json.loads(authenticated_config.stdout)["services"][
            "pek-dev"
        ]["build"]["args"]
        self.assertEqual(authenticated_args["HF_DOWNLOAD_CACHEBUST"], "")

    def test_main_compose_uses_model_bearing_target(self) -> None:
        docker = shutil.which("docker")
        docker_required = os.environ.get("PEK_REQUIRE_DOCKER_BUILD_TEST") == "1"
        if docker is None:
            if docker_required:
                self.fail("Docker CLI is required")
            self.skipTest(DOCKER_UNAVAILABLE)
        if subprocess.run(
            [docker, "buildx", "version"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        ).returncode:
            if docker_required:
                self.fail("Docker Buildx is required")
            self.skipTest("Docker Buildx is not installed")

        env = os.environ.copy()
        env["HF_TOKEN"] = ""
        env["HF_DOWNLOAD_CACHEBUST"] = "outline-key"
        outline = subprocess.run(
            [
                docker,
                "buildx",
                "bake",
                "-f",
                COMPOSE_FILE,
                "--call=outline",
                "pek-dev",
            ],
            cwd=REPO_ROOT,
            env=env,
            text=True,
            capture_output=True,
            check=False,
        )
        output = outline.stdout + outline.stderr
        self.assertEqual(outline.returncode, 0, output)
        self.assertRegex(output, r"(?m)^TARGET:\s+pek-deployment-base$")
        self.assertRegex(
            output,
            r"(?m)^HF_DOWNLOAD_CACHEBUST\s+outline-key\s+",
        )

        dockerfile = (REPO_ROOT / "Dockerfile").read_text()
        deployment_build = dockerfile.split(
            " AS pek-deployment-build", 1
        )[1].split(" AS pek-deployment-base", 1)[0]
        self.assertIn("COPY --from=pek-models /work/config config", deployment_build)

    def test_raw_model_build_requires_cache_key(self) -> None:
        docker = shutil.which("docker")
        docker_required = os.environ.get("PEK_REQUIRE_DOCKER_BUILD_TEST") == "1"
        if docker is None:
            if docker_required:
                self.fail("Docker CLI is required")
            self.skipTest(DOCKER_UNAVAILABLE)
        if subprocess.run(
            [docker, "info"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        ).returncode:
            if docker_required:
                self.fail("Docker daemon is required")
            self.skipTest("Docker daemon is not available")

        with tempfile.TemporaryDirectory() as temporary_directory:
            context = Path(temporary_directory)
            shutil.copy2(REPO_ROOT / "Dockerfile", context / "Dockerfile")
            shutil.copytree(
                REPO_ROOT / SCHEMAS_DIR,
                context / SCHEMAS_DIR,
            )
            models = context / "config/models"
            models.mkdir()
            (models / "README").write_text("No model downloads needed.\n")
            scripts = context / "scripts"
            scripts.mkdir()
            shutil.copy2(DOWNLOAD_SCRIPT, scripts / DOWNLOAD_SCRIPT.name)

            env = os.environ.copy()
            env.pop("HF_TOKEN", None)
            env.pop("HF_DOWNLOAD_CACHEBUST", None)
            cachebust_arg = "HF_DOWNLOAD_CACHEBUST="

            def build(*arguments: str) -> subprocess.CompletedProcess[str]:
                return subprocess.run(
                    [
                        docker,
                        "build",
                        "--target",
                        "pek-models",
                        *arguments,
                        "--progress=plain",
                        ".",
                    ],
                    cwd=context,
                    env=env,
                    text=True,
                    capture_output=True,
                    check=False,
                )

            anonymous = build(
                "--build-arg",
                cachebust_arg + "anon",
            )
            self.assertEqual(
                anonymous.returncode,
                0,
                anonymous.stdout + anonymous.stderr,
            )

            missing_anonymous = build()
            missing_anonymous_output = (
                missing_anonymous.stdout + missing_anonymous.stderr
            )
            self.assertNotEqual(
                missing_anonymous.returncode,
                0,
                missing_anonymous_output,
            )
            self.assertIn(
                "HF_DOWNLOAD_CACHEBUST is required for model image builds",
                missing_anonymous_output,
            )

            env["HF_TOKEN"] = "cache-key-contract-test"
            missing_authenticated = build(
                "--secret",
                "id=huggingface_token,env=HF_TOKEN",
            )
            missing_authenticated_output = (
                missing_authenticated.stdout + missing_authenticated.stderr
            )
            self.assertNotEqual(
                missing_authenticated.returncode,
                0,
                missing_authenticated_output,
            )
            self.assertIn(
                "HF_DOWNLOAD_CACHEBUST is required for model image builds",
                missing_authenticated_output,
            )

            authenticated = build(
                "--secret",
                "id=huggingface_token,env=HF_TOKEN",
                "--build-arg",
                cachebust_arg + "auth",
            )
            self.assertEqual(
                authenticated.returncode,
                0,
                authenticated.stdout + authenticated.stderr,
            )

    def test_model_download_cache_bust_generator(self) -> None:
        generator = REPO_ROOT / "scripts/private/generate-hf-download-cachebust.sh"
        local_env = os.environ.copy()
        for name in ("GITHUB_ACTIONS", "GITHUB_RUN_ID", "GITHUB_RUN_ATTEMPT"):
            local_env.pop(name, None)

        first = subprocess.check_output([generator], env=local_env, text=True).strip()
        second = subprocess.check_output([generator], env=local_env, text=True).strip()
        self.assertRegex(
            first,
            r"^local-[0-9]{8}T[0-9]{6}Z-[0-9a-f]{32}$",
        )
        self.assertNotEqual(first, second)

        github_env = local_env | {
            "GITHUB_ACTIONS": "true",
            "GITHUB_RUN_ID": "123456",
            "GITHUB_RUN_ATTEMPT": "7",
        }
        self.assertEqual(
            subprocess.check_output([generator], env=github_env, text=True).strip(),
            "github-123456-7",
        )
        github_env.pop("GITHUB_RUN_ID")
        self.assertNotEqual(
            subprocess.run(
                [generator],
                env=github_env,
                text=True,
                capture_output=True,
                check=False,
            ).returncode,
            0,
        )

    def test_model_artifacts_are_ignored_except_checked_in_models(self) -> None:
        expected = [
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
            shutil.copytree(
                REPO_ROOT / SCHEMAS_DIR,
                root / SCHEMAS_DIR,
            )

            for name, model_file, hub_file in (
                ("first", "missing.onnx", "missing.onnx"),
                ("second", "renamed.bin", "available.onnx"),
            ):
                model_dir = root / "config" / "models" / name
                model_dir.mkdir(parents=True)
                (model_dir / MODEL_DESCRIPTOR).write_text(
                    json.dumps(
                        {
                            "version": 1,
                            "name": name,
                            "modelFile": model_file,
                            "hfDownload": {
                                "repo_id": "test/repo",
                                "revision": "0123456789abcdef0123456789abcdef01234567",
                                "filename": hub_file,
                            },
                            "dynamicOutput": True,
                            "inputTensors": [
                                {
                                    "shape": [1],
                                    "dataKind": "RawTensorData",
                                    "valueType": "Float32",
                                }
                            ],
                        }
                    )
                )

            externally_managed = root / "config" / "models" / "external"
            externally_managed.mkdir(parents=True)
            (externally_managed / MODEL_DESCRIPTOR).write_text(
                json.dumps(
                    {
                        "version": 1,
                        "name": "external",
                        "modelFile": "/opt/models/external.onnx",
                        "dynamicOutput": True,
                        "inputTensors": [
                            {
                                "shape": [1],
                                "dataKind": "RawTensorData",
                                "valueType": "Float32",
                            }
                        ],
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
                "HF_TOKEN": "",
                "HF_TOKEN_CAPTURE": str(root / "captured-token"),
                "PYTHONPATH": str(fake_hub.parent),
            }

            def run_download(*extra_args, check=False):
                return subprocess.run(
                    [
                        sys.executable,
                        str(scripts / DOWNLOAD_SCRIPT.name),
                        "--models-dir",
                        MODELS_DIR,
                        *extra_args,
                    ],
                    check=check,
                    cwd=root,
                    env=environment,
                    capture_output=True,
                    text=True,
                )

            result = run_download()
            self.assertEqual(result.returncode, 0, result.stderr)

            self.assertFalse((root / "config/models/first/missing.onnx").exists())
            self.assertEqual(
                (root / "config/models/second/renamed.bin").read_text(),
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
                "config/models/second/renamed.bin uses .bin; saving as configured.",
                result.stderr,
            )
            anonymous_capture = (root / "captured-token").read_text().splitlines()
            self.assertEqual(
                anonymous_capture,
                ["False", str(root / "hub-cache/hub/anonymous")],
            )

            environment["HF_TOKEN"] = "test-token"
            run_download(check=True)
            first_token_capture = (
                root / "captured-token"
            ).read_text().splitlines()
            first_token_cache = Path(first_token_capture[1])
            self.assertEqual(first_token_capture[0], "'test-token'")
            self.assertEqual(first_token_cache.parent, root / "hub-cache/hub")
            self.assertNotEqual(first_token_cache, Path(anonymous_capture[1]))
            self.assertNotIn("test-token", first_token_cache.name)

            environment["HF_TOKEN"] = "lower-access-token"
            run_download(check=True)
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
            self.assertNotIn("--token", help_result.stdout)

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

            unsafe = root / "config/models/unsafe"
            unsafe.mkdir()
            unsafe_model = json.loads(
                (root / "config/models/second/model.json").read_text()
            )
            unsafe_model["name"] = "unsafe"
            del unsafe_model["modelFile"]
            (unsafe / MODEL_DESCRIPTOR).write_text(json.dumps(unsafe_model))
            (root / "captured-token").unlink(missing_ok=True)

            missing_destination_failure = run_download()
            self.assertEqual(missing_destination_failure.returncode, 1)
            self.assertIn("modelFile", missing_destination_failure.stderr)
            self.assertIn("required property", missing_destination_failure.stderr)
            self.assertNotIn("Traceback", missing_destination_failure.stderr)
            self.assertFalse((root / "captured-token").exists())

            unsafe_model["modelFile"] = "../escape.onnx"
            (unsafe / MODEL_DESCRIPTOR).write_text(json.dumps(unsafe_model))

            schema_failure = run_download()
            self.assertNotEqual(schema_failure.returncode, 0)
            self.assertIn("Invalid model descriptor", schema_failure.stderr)
            self.assertIn("../escape.onnx", schema_failure.stderr)
            self.assertNotIn("Traceback", schema_failure.stderr)
            self.assertFalse((root / "captured-token").exists())

            outside = root / "outside"
            outside.mkdir()
            (unsafe / "linked").symlink_to(outside, target_is_directory=True)
            unsafe_model["modelFile"] = "linked/escape.onnx"
            (unsafe / MODEL_DESCRIPTOR).write_text(json.dumps(unsafe_model))

            containment_failure = run_download()
            self.assertNotEqual(containment_failure.returncode, 0)
            self.assertIn(
                "modelFile resolves outside its model directory",
                containment_failure.stderr,
            )
            self.assertNotIn("Traceback", containment_failure.stderr)
            self.assertFalse((root / "captured-token").exists())

            (unsafe / "blocked").write_text("not a directory")
            unsafe_model["modelFile"] = "blocked/model.onnx"
            (unsafe / MODEL_DESCRIPTOR).write_text(json.dumps(unsafe_model))

            destination_failure = run_download()
            self.assertEqual(destination_failure.returncode, 1)
            self.assertIn(
                "Cannot prepare modelFile destination",
                destination_failure.stderr,
            )
            self.assertNotIn("Traceback", destination_failure.stderr)
            self.assertFalse((root / "captured-token").exists())

            (unsafe / "directory.onnx").mkdir()
            unsafe_model["modelFile"] = "directory.onnx"
            (unsafe / MODEL_DESCRIPTOR).write_text(json.dumps(unsafe_model))

            destination_type_failure = run_download()
            self.assertEqual(destination_type_failure.returncode, 1)
            self.assertIn(
                "modelFile destination is not a file",
                destination_type_failure.stderr,
            )
            self.assertNotIn("Traceback", destination_type_failure.stderr)
            self.assertFalse((root / "captured-token").exists())

            (unsafe / MODEL_DESCRIPTOR).write_text("{")
            json_failure = run_download()
            self.assertEqual(json_failure.returncode, 1)
            self.assertIn(
                "config/models/unsafe/model.json",
                json_failure.stderr,
            )
            self.assertNotIn("Traceback", json_failure.stderr)
            self.assertFalse((root / "captured-token").exists())

            (root / MODEL_SCHEMA).write_text(
                json.dumps({"type": 7})
            )
            schema_definition_failure = run_download()
            self.assertEqual(schema_definition_failure.returncode, 1)
            self.assertIn("Invalid model schema", schema_definition_failure.stderr)
            self.assertNotIn("Traceback", schema_definition_failure.stderr)
            self.assertFalse((root / "captured-token").exists())

            schema = json.loads(
                (REPO_ROOT / MODEL_SCHEMA).read_text()
            )
            del schema["$defs"]["safeRelativePath"]
            (root / MODEL_SCHEMA).write_text(
                json.dumps(schema)
            )
            unresolved_reference_failure = run_download()
            self.assertEqual(unresolved_reference_failure.returncode, 1)
            self.assertIn("Invalid model schema", unresolved_reference_failure.stderr)
            self.assertIn(
                "unresolved reference /$defs/safeRelativePath",
                unresolved_reference_failure.stderr,
            )
            self.assertNotIn("Traceback", unresolved_reference_failure.stderr)
            self.assertFalse((root / "captured-token").exists())


if __name__ == "__main__":
    unittest.main()
