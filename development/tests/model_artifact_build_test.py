#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import hashlib
import json
import os
import runpy
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

REPO_ROOT = Path(__file__).resolve().parents[2]
DOWNLOAD_SCRIPT = REPO_ROOT / "scripts" / "download-models.py"
COMPOSE_FILE = "compose.yaml"
DOCKER_UNAVAILABLE = "Docker CLI is not installed"
MODELS_DIR = "config/models"
MODEL_DESCRIPTOR = "model.json"
SCHEMAS_DIR = Path("config/schemas")
MODEL_SCHEMA = SCHEMAS_DIR / "v1/model.schema.json"
# Temporary EXPKITS-1084 quality gate while stale model artifacts may remain in build contexts.
RETIRED_MODEL_SUFFIXES = {".hef"}


class ModelArtifactBuildTest(unittest.TestCase):
    def test_downloader_uses_shared_supported_version(self) -> None:
        downloader = runpy.run_path(str(DOWNLOAD_SCRIPT))

        with tempfile.TemporaryDirectory() as directory:
            descriptor = Path(directory) / MODEL_DESCRIPTOR
            schema = Path(directory) / "model.schema.json"
            model = json.loads((REPO_ROOT / "config/models/yolo26n-320/model.json").read_text())
            schema_document = json.loads((REPO_ROOT / MODEL_SCHEMA).read_text())
            schema_document["x-opk-supported-version"] = "2.3.4"
            schema.write_text(json.dumps(schema_document))
            model.pop("hfDownload", None)
            module_globals = downloader["_prepare_download"].__globals__
            with mock.patch.dict(module_globals, {"MODEL_SCHEMA": schema}):
                validator = downloader["_load_validator"]()
                for version in ("2.3.99", "2.0.0", "1.0.0"):
                    model["version"] = version
                    descriptor.write_text(json.dumps(model))
                    with self.subTest(version=version):
                        if version == "1.0.0":
                            with self.assertRaisesRegex(ValueError, "supported version: 2.3.4"):
                                downloader["_prepare_download"](descriptor, validator)
                        elif version == "2.0.0":
                            with self.assertLogs(module_globals["LOGGER"], level="WARNING") as messages:
                                downloader["_prepare_download"](descriptor, validator)
                            self.assertIn("supported version: 2.3.4", messages.output[0])
                        else:
                            with self.assertNoLogs(downloader["LOGGER"], level="WARNING"):
                                downloader["_prepare_download"](descriptor, validator)

    def test_configuration_schema_versions_require_complete_strings(self) -> None:
        from jsonschema import Draft202012Validator

        for name in ("model", "opchain", "pipeline"):
            schema = json.loads((REPO_ROOT / SCHEMAS_DIR / f"v1/{name}.schema.json").read_text())
            validator = Draft202012Validator(schema["properties"]["version"])
            for version in ("1.0.0", "1.0.37", "1.4.2", "0.9.0", "2.0.0"):
                with self.subTest(name=name, version=version):
                    self.assertTrue(validator.is_valid(version))
            for version in (None, 1, True, "1", "1.0.0\n", "1.0.0\0",
                            "1.0.0.0", "01.0.0", "1.01.0", "1.0.01", "1.0.0-rc1", "1.0.0+build"):
                with self.subTest(name=name, version=version):
                    self.assertFalse(validator.is_valid(version))

    def test_defaults_use_yolo26n_320(self) -> None:
        pipeline = json.loads(
            (REPO_ROOT / "config/pipelines/yolo26n-320.json").read_text()
        )
        inference_steps = [
            step for step in pipeline["pipeline"] if "opkinfer" in step
        ]
        self.assertEqual(
            inference_steps,
            [
                'opkinfer opchain-path="${OPK_PROJECT_ROOT:-/work}/config/models/'
                'yolo26n-320/opchain.json" active=true !'
            ],
        )
        self.assertIn(
            "OPK_MENU_ARGS=(yolo26n-320)",
            (REPO_ROOT / "scripts/run.sh").read_text(),
        )
        self.assertIn(
            "OPK_PIPELINE: ${OPK_PIPELINE:-yolo26n-320}",
            (REPO_ROOT / COMPOSE_FILE).read_text(),
        )
        self.assertIn(
            "ARG OPK_PIPELINE=yolo26n-320",
            (REPO_ROOT / "Dockerfile").read_text(),
        )
        for script in (
            "scripts/private/deployment-process.sh",
            "scripts/private/deployment-runtime.sh",
        ):
            self.assertIn(
                'OPK_PIPELINE=${OPK_PIPELINE:-"yolo26n-320"}',
                (REPO_ROOT / script).read_text(),
            )
        descriptor = json.loads(
            (REPO_ROOT / "config/models/yolo26n-320/model.json").read_text()
        )
        self.assertEqual(
            descriptor["hfDownload"]["repo_id"],
            "Arm/yolo26n-320-int8-onnx-raspberrypi5",
        )
        self.assertEqual(
            descriptor["modelFile"],
            "yolo26n_raspberry_onnx_optimized.onnx",
        )

    def test_vscode_pipeline_choices_match_catalog(self) -> None:
        for filename in (".vscode/tasks.json", ".vscode/launch.json"):
            with self.subTest(filename=filename):
                config = json.loads((REPO_ROOT / filename).read_text())
                picker = next(item for item in config["inputs"]
                              if item["id"] == "selectedPipeline")
                self.assertEqual(picker["default"], "yolo26n-320")
                self.assertIn(picker["default"], picker["options"])
                for pipeline in picker["options"]:
                    self.assertTrue(
                        (REPO_ROOT / "config/pipelines" / f"{pipeline}.json").is_file(),
                        pipeline,
                    )

    def test_dev_container_seeds_downloaded_artifacts(self) -> None:
        dockerfile = (REPO_ROOT / "Dockerfile").read_text()
        entrypoint = (
            REPO_ROOT / "scripts/private/development-entrypoint.sh"
        ).read_text()
        runtime_stage = dockerfile.split(" AS opk-dev-base", 1)[1].split(
            "FROM opk-dev-base AS opk-dev-tools", 1
        )[0]
        dev_tools_stage = dockerfile.split(" AS opk-dev-tools", 1)[1].split(
            "FROM opk-dev-tools AS opk-dev", 1
        )[0]
        self.assertIn("-r /opt/opk-deps/requirements/models.txt", runtime_stage)
        self.assertRegex(dev_tools_stage, r"\bffmpeg\b")
        self.assertIn("-r /opt/opk-deps/requirements/common.txt", runtime_stage)
        self.assertIn(
            "COPY --from=opk-models \\\n"
            "  /work/config/models /opt/opk-app/config/models",
            dockerfile,
        )
        self.assertIn(
            '"${OPK_PROJECT_ROOT}/config/models/"',
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
        self.assertIn(
            "service: opk-common-dev-model-image",
            (REPO_ROOT / ".devcontainer/compose.devcont.yaml").read_text(),
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
        build_args = json.loads(config.stdout)["services"]["opk-dev"]["build"][
            "args"
        ]
        self.assertNotIn("HF_DOWNLOAD_CACHEBUST", build_args)

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
            "opk-dev"
        ]["build"]["args"]
        self.assertNotIn("HF_DOWNLOAD_CACHEBUST", authenticated_args)

    def test_main_compose_build_does_not_download_models(self) -> None:
        docker = shutil.which("docker")
        docker_required = os.environ.get("OPK_REQUIRE_DOCKER_BUILD_TEST") == "1"
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
                "opk-dev",
            ],
            cwd=REPO_ROOT,
            env=env,
            text=True,
            capture_output=True,
            check=False,
        )
        output = outline.stdout + outline.stderr
        self.assertEqual(outline.returncode, 0, output)
        self.assertRegex(output, r"(?m)^TARGET:\s+opk-deployment-base$")
        self.assertNotIn("HF_DOWNLOAD_CACHEBUST", output)
        self.assertNotIn("huggingface_token", output)

        dockerfile = (REPO_ROOT / "Dockerfile").read_text()
        deployment_build = dockerfile.split(
            " AS opk-deployment-build", 1
        )[1].split(" AS opk-deployment-base", 1)[0]
        self.assertIn("COPY --from=opk-release-sources /work/config config", deployment_build)
        self.assertNotIn("--from=opk-models", deployment_build)
        cairn_build = dockerfile.split(" AS opk-cairn-build", 1)[1]
        self.assertNotIn("--from=opk-models", cairn_build)
        for stage in (deployment_build, cairn_build):
            for line in stage.splitlines():
                if line.startswith("COPY "):
                    self.assertIn("--from=", line)

    def test_raw_model_build_requires_cache_key(self) -> None:
        docker = shutil.which("docker")
        docker_required = os.environ.get("OPK_REQUIRE_DOCKER_BUILD_TEST") == "1"
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
            shutil.copytree(REPO_ROOT / "requirements", context / "requirements")
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

            (context / "tools").mkdir()
            shutil.copy2(REPO_ROOT / "tools/config_versions.py", context / "tools/config_versions.py")

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
                        "opk-models",
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

    def test_release_sources_exclude_user_models_everywhere(self) -> None:
        docker = shutil.which("docker")
        if docker is None:
            self.skipTest(DOCKER_UNAVAILABLE)
        with tempfile.TemporaryDirectory() as temporary:
            context = Path(temporary) / "context"
            context.mkdir()
            shutil.copy2(REPO_ROOT / ".dockerignore", context / ".dockerignore")
            dockerfile = (REPO_ROOT / "Dockerfile").read_text()
            source_stage = next(
                "FROM " + stage for stage in dockerfile.split("\nFROM ")
                if stage.splitlines()[0].endswith(" AS opk-release-sources")
            )
            (context / "Dockerfile").write_text(
                source_stage + "\nFROM scratch\nCOPY --from=opk-release-sources /work/ /\n"
            )
            copier = Path("scripts/private/copy-without-models.py")
            (context / copier).parent.mkdir(parents=True)
            shutil.copy2(REPO_ROOT / copier, context / copier)
            for relative in (
                "config/models/example/model.onnx",
                "config/models/example/renamed.bin",
                "config/models/example/renamed.bin.part",
                "development/examples/example/model.pte",
                "development/examples/example/renamed.bin",
                "data/models/renamed.bin.part",
                "tools/perception/model.onnx.part",
                "var/downloads/model.pte",
                "var/downloads/model.pte.part",
            ):
                path = context / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"must not be published")
            descriptor = context / "config/models/example/model.json"
            descriptor.write_text("{}\n")
            expected = {"Dockerfile", ".dockerignore", "config/models/example/model.json", str(copier)}
            for directory, filename in (
                ("config/models/custom", "weights.dat"),
                ("development/examples/custom", "weights"),
                ("data/models/custom", "weights.ONNX"),
                ("config/models/nested", "nested/weights.dat"),
            ):
                model_root = context / directory
                model_root.mkdir(parents=True)
                (model_root / "model.json").write_text(json.dumps({"modelFile": filename}))
                (model_root / filename).parent.mkdir(parents=True, exist_ok=True)
                (model_root / filename).write_bytes(b"must not be published")
                (model_root / f"{filename}.part").write_bytes(b"partial download")
                expected.add(f"{directory}/model.json")
                (model_root / "postprocess.py").write_text("# Keep model-local source files.\n")
                expected.add(f"{directory}/postprocess.py")
            (context / "config/models/custom/model.json").write_text(
                json.dumps({"modelFile": "/work/../work/config/models/custom/weights.dat"})
            )
            output = Path(temporary) / "output"
            result = subprocess.run(
                [docker, "buildx", "build", "--output", f"type=local,dest={output}", str(context)],
                text=True, capture_output=True, check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(
                {str(path.relative_to(output)) for path in output.rglob("*") if path.is_file()},
                expected,
            )

    def test_release_smoke_needs_no_model_downloads(self) -> None:
        docker = shutil.which("docker")
        if docker is None:
            self.skipTest(DOCKER_UNAVAILABLE)
        release = runpy.run_path(str(REPO_ROOT / "scripts/release/ReleaseTool.py"))
        with tempfile.TemporaryDirectory() as temporary:
            context = Path(temporary)
            package = context / "opk-1.0.0-linux-x86_64"
            release["stage_models"](
                SimpleNamespace(repo_root=str(REPO_ROOT), stage_root=str(package))
            )
            archive = context / "package.tar.gz"
            with tarfile.open(archive, "w:gz") as stream:
                stream.add(package, arcname=package.name)
            digest = hashlib.sha256(archive.read_bytes()).hexdigest()
            shutil.copy2(REPO_ROOT / "scripts/release/smoke-opk-package.sh", context / "smoke.sh")
            operation = context / "development/tests/python_script_op/runtime_environment.py"
            operation.parent.mkdir(parents=True)
            operation.touch()
            gst_stub = context / "gst-stub"
            gst_stub.write_text("""#!/usr/local/bin/python3
import json
import os
import sys
from pathlib import Path

root = Path(os.environ['GST_PLUGIN_PATH']).parents[1] / 'share/opk/models'
assert not [p for p in root.rglob('*') if p.suffix in ('.onnx', '.pte', '.bin')]
for arg in sys.argv:
    if arg.startswith('opchain-path='):
        opchain = json.loads(Path(arg.removeprefix('opchain-path=')).read_text())
        assert [op['id'] for op in opchain['ops']] == ['opk-python-ops/PythonScript']
        script = Path(opchain['ops'][0]['attributes']['script'])
        assert script.is_absolute() and script.is_file()
""")
            gst_stub.chmod(0o755)
            dockerfile = (REPO_ROOT / "Dockerfile").read_text()
            stage_start = 'ARG TARGETARCH\nARG OPK_RELEASE_VERSION=""\nRUN --network=none'
            smoke_stage = stage_start + dockerfile.split(stage_start, 1)[1].split("\n# ===", 1)[0]
            (context / "Dockerfile").write_text(
                "FROM python:3.13-slim-trixie AS opk-models\n"
                "RUN exit 99\n"
                "FROM python:3.13-slim-trixie AS opk-deployment-base\n"
                "COPY package.tar.gz /opt/opk-release-artifacts/opk-1.0.0-linux-x86_64.tar.gz\n"
                "COPY smoke.sh /work/scripts/release/smoke-opk-package.sh\n"
                "COPY gst-stub /usr/local/bin/gst-inspect-1.0\n"
                "COPY gst-stub /usr/local/bin/gst-launch-1.0\n"
                "USER 65534:65534\n"
                + smoke_stage
                + "\nRUN python3 -c \"import hashlib; from pathlib import Path; "
                "assert not [p for root in ('/tmp', '/work', '/opt') for p in Path(root).rglob('*') "
                "if p.suffix in ('.onnx', '.pte')]; "
                "assert hashlib.sha256(Path('/opt/opk-release-artifacts/"
                f"opk-1.0.0-linux-x86_64.tar.gz').read_bytes()).hexdigest() == '{digest}'\"\n"
            )
            result = subprocess.run(
                [docker, "buildx", "build", "--target", "opk-deployment-base", "--build-arg", "OPK_RELEASE_VERSION=1.0.0",
                 "--build-arg", "TARGETARCH=amd64", "--output", "type=cacheonly", str(context)],
                text=True, capture_output=True, check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

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

    def test_model_artifacts_are_excluded_from_source_and_image_contexts(self) -> None:
        expected = [
            "config/models/**/*.bin",
            "config/models/**/*.bin.part",
            "config/models/**/*.hef",
            "config/models/**/*.onnx",
            "config/models/**/*.pte",
        ]
        rules = [
            line
            for line in (REPO_ROOT / ".gitignore").read_text().splitlines()
            if line.startswith(("config/models/", "!config/models/"))
        ]
        self.assertEqual(rules, expected)
        dockerignore = (REPO_ROOT / ".dockerignore").read_text().splitlines()
        for pattern in ("**/*.bin", "**/*.bin.part", "**/*.hef", "**/*.onnx", "**/*.pte", "**/*.onnx.part", "**/*.pte.part"):
            self.assertIn(pattern, dockerignore)

    def test_byom_generated_artifacts_are_excluded_from_docker_context(self) -> None:
        dockerignore = (REPO_ROOT / ".dockerignore").read_text().splitlines()
        self.assertEqual(
            [
                line
                for line in dockerignore
                if line.startswith("development/examples/byom-blazeface/")
            ],
            [
                "development/examples/byom-blazeface/blazeface-detections.mp4",
                "development/examples/byom-blazeface/.blazeface-detections.part.mp4",
            ],
        )

    def test_retired_model_artifacts_are_absent(self) -> None:
        model_root = REPO_ROOT / MODELS_DIR
        retired = sorted(
            str(path.relative_to(REPO_ROOT))
            for path in model_root.rglob("*")
            if path.is_file() and path.suffix.casefold() in RETIRED_MODEL_SUFFIXES
        )
        self.assertEqual(retired, [], f"retired model artifacts found: {retired}")

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
            (root / "tools").mkdir()
            shutil.copy2(REPO_ROOT / "tools/config_versions.py", root / "tools/config_versions.py")
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
                            "version": "1.0.0",
                            "name": name,
                            "modelFile": model_file,
                            "hfDownload": {
                                "repo_id": "test/repo",
                                "revision": "0123456789abcdef0123456789abcdef01234567",
                                "filename": hub_file,
                                **(
                                    {
                                        "sha256": "9372c470eeadd5ecd9c3c74c2b3cb633f8e2f2fad799250a0f70d652b6b825e4"
                                    }
                                    if name == "second"
                                    else {}
                                ),
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
                        "version": "1.0.0",
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
            self.assertFalse(
                (root / "config/models/second/renamed.bin.part").exists()
            )
            anonymous_capture = (root / "captured-token").read_text().splitlines()
            self.assertEqual(
                anonymous_capture,
                ["False", str(root / "hub-cache/hub/anonymous")],
            )

            verified_descriptor = root / "config/models/second/model.json"
            mismatched_model = json.loads(verified_descriptor.read_text())
            for version in (1, "0.9.0", "2.0.0", "1.0.0\n", "1.0.0\0", "1.0.0-rc1"):
                with self.subTest(version=version):
                    mismatched_model["version"] = version
                    verified_descriptor.write_text(json.dumps(mismatched_model))
                    (root / "captured-token").unlink(missing_ok=True)
                    version_result = run_download()
                    self.assertEqual(version_result.returncode, 1, version_result.stderr)
                    if version in ("0.9.0", "2.0.0"):
                        self.assertIn("/version", version_result.stderr)
                    else:
                        self.assertIn("Invalid model descriptor", version_result.stderr)
                    self.assertFalse((root / "captured-token").exists())
            for version in ("1.0.37", "1.4.2"):
                with self.subTest(version=version):
                    mismatched_model["version"] = version
                    verified_descriptor.write_text(json.dumps(mismatched_model))
                    version_result = run_download()
                    self.assertEqual(version_result.returncode, 0, version_result.stderr)
                    self.assertTrue((root / "captured-token").exists())
                    self.assertEqual("different minor" in version_result.stderr, version == "1.4.2")
            mismatched_model["version"] = "1.0.0"
            mismatched_model["hfDownload"]["sha256"] = "0" * 64
            verified_descriptor.write_text(json.dumps(mismatched_model))
            installed_model = root / "config/models/second/renamed.bin"
            installed_model.write_text("existing")

            mismatch_result = run_download()
            self.assertEqual(mismatch_result.returncode, 0, mismatch_result.stderr)
            self.assertFalse(installed_model.exists())
            self.assertFalse(installed_model.with_name("renamed.bin.part").exists())
            self.assertIn("SHA-256 mismatch", mismatch_result.stderr)

            mismatched_model["hfDownload"]["sha256"] = (
                "9372c470eeadd5ecd9c3c74c2b3cb633f8e2f2fad799250a0f70d652b6b825e4"
            )
            verified_descriptor.write_text(json.dumps(mismatched_model))

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
