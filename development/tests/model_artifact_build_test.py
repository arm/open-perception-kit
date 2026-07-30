#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DOWNLOAD_SCRIPT = REPO_ROOT / "scripts" / "download-models.py"
HF_DOWNLOAD_KEYS = {"repo_id", "revision", "filename"}


def model_descriptors() -> list[tuple[Path, dict]]:
    descriptors = []
    for descriptor in (REPO_ROOT / "config" / "models").glob("*/*.json"):
        data = json.loads(descriptor.read_text())
        if "modelFile" in data:
            descriptors.append((descriptor, data))
    return sorted(descriptors)


class ModelArtifactBuildTest(unittest.TestCase):
    def test_dev_seed_removes_stale_skipped_download(self) -> None:
        entrypoint = (
            REPO_ROOT / "scripts/private/development-entrypoint.sh"
        ).read_text()
        seed_script = re.search(
            r"python3 - .*? << 'PY'\n(.*?)\nPY\n", entrypoint, re.DOTALL
        )
        self.assertIsNotNone(seed_script)

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
                input=seed_script.group(1),
                check=True,
                text=True,
            )

            self.assertFalse(stale_artifact.exists())

    def test_downloads_every_published_model_to_its_descriptor_path(self) -> None:
        descriptors = model_descriptors()
        expected = set()
        local_model_files = set()
        expected_downloads = []
        for descriptor, model in descriptors:
            source = model.get("hfDownload")
            model_file = descriptor.parent.relative_to(REPO_ROOT) / model["modelFile"]
            if source is None:
                local_model_files.add(model_file)
                self.assertTrue((REPO_ROOT / model_file).is_file())
                continue
            expected.add(model_file)
            self.assertEqual(set(source), HF_DOWNLOAD_KEYS)
            self.assertRegex(source["revision"], re.compile(r"^[0-9a-f]{40}$"))
            expected_downloads.append((model_file, source))
        expected_calls = [source for _, source in expected_downloads]
        self.assertTrue(expected)
        self.assertTrue(local_model_files)
        for ignore_file in (".dockerignore", ".gitignore"):
            ignored_models = {
                Path(line)
                for line in (REPO_ROOT / ignore_file).read_text().splitlines()
                if line.startswith("config/models/")
            }
            self.assertEqual(ignored_models, expected)

        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            scripts = root / "scripts"
            fake_pythonpath = root / "pythonpath"
            fake_cache = root / "cache"
            scripts.mkdir()
            fake_pythonpath.mkdir()
            fake_cache.mkdir()
            shutil.copy2(DOWNLOAD_SCRIPT, scripts / DOWNLOAD_SCRIPT.name)
            for descriptor, _ in descriptors:
                copy = root / descriptor.relative_to(REPO_ROOT)
                copy.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(descriptor, copy)

            fake_hub = fake_pythonpath / "huggingface_hub"
            fake_hub.mkdir()
            (fake_hub / "errors.py").write_text(
                """class HfHubHTTPError(OSError):
    def __init__(self, status_code):
        self.response = type("Response", (), {"status_code": status_code})()
"""
            )
            (fake_hub / "__init__.py").write_text(
                """import json
import os
from pathlib import Path

from .errors import HfHubHTTPError


class HfApi:
    def whoami(self, token=None):
        if token == "invalid":
            raise HfHubHTTPError(401)
        if token == "server":
            raise HfHubHTTPError(500)
        if token == "network":
            raise RuntimeError("connection failed")
        return {"name": "test"}


def hf_hub_download(*, repo_id, revision, filename, token):
    log = Path(os.environ["HF_FAKE_LOG"])
    calls = log.read_text().splitlines() if log.exists() else []
    log.write_text("\\n".join(calls + [json.dumps({
        "repo_id": repo_id,
        "revision": revision,
        "filename": filename,
        "token": token,
    })]) + "\\n")
    if f"{repo_id}/{filename}" == os.environ.get("HF_FAKE_DENIED_SOURCE"):
        raise HfHubHTTPError(int(os.environ["HF_FAKE_DENIED_STATUS"]))
    download = Path(os.environ["HF_FAKE_CACHE"]) / str(len(calls))
    download.write_text("model\\n")
    return download
"""
            )
            log = root / "hf.log"
            environment = {
                key: value
                for key, value in os.environ.items()
                if key != "HF_TOKEN"
            } | {
                "HF_FAKE_CACHE": str(fake_cache),
                "HF_FAKE_LOG": str(log),
                "PYTHONPATH": str(fake_pythonpath),
            }

            denied_model_file, denied_source = expected_downloads[0]
            denied_source_id = (
                f"{denied_source['repo_id']}/{denied_source['filename']}"
            )
            anonymous = subprocess.run(
                [sys.executable, str(scripts / DOWNLOAD_SCRIPT.name)],
                check=True,
                env=environment
                | {
                    "HF_FAKE_DENIED_SOURCE": denied_source_id,
                    "HF_FAKE_DENIED_STATUS": "401",
                },
                capture_output=True,
                text=True,
            )
            downloaded_files = {
                path.relative_to(root)
                for path in (root / "config" / "models").rglob("*")
                if path.is_file() and path.suffix != ".json"
            }
            self.assertEqual(downloaded_files, expected - {denied_model_file})
            calls = [json.loads(line) for line in log.read_text().splitlines()]
            self.assertEqual(
                calls,
                [source | {"token": False} for source in expected_calls],
            )
            self.assertIn(
                "No Hugging Face token supplied; downloading public models "
                "anonymously.\n",
                anonymous.stdout,
            )
            self.assertIn(
                f"Skipping {denied_model_file}: an authorized Hugging Face "
                "token is required.\n",
                anonymous.stdout,
            )

            for model_file in expected:
                (root / model_file).unlink(missing_ok=True)
            log.unlink()

            limited = subprocess.run(
                [sys.executable, str(scripts / DOWNLOAD_SCRIPT.name)],
                check=True,
                env=environment
                | {
                    "HF_TOKEN": "access-limited",
                    "HF_FAKE_DENIED_SOURCE": denied_source_id,
                    "HF_FAKE_DENIED_STATUS": "403",
                },
                capture_output=True,
                text=True,
            )
            downloaded_files = {
                path.relative_to(root)
                for path in (root / "config" / "models").rglob("*")
                if path.is_file() and path.suffix != ".json"
            }
            self.assertEqual(downloaded_files, expected - {denied_model_file})
            calls = [json.loads(line) for line in log.read_text().splitlines()]
            self.assertEqual(
                calls,
                [
                    source | {"token": "access-limited"}
                    for source in expected_calls
                ],
            )
            self.assertIn(
                "Connected to Hugging Face with the supplied token.\n",
                limited.stdout,
            )
            self.assertIn(
                f"Skipping {denied_model_file}: the supplied Hugging Face "
                "token does not grant access.\n",
                limited.stdout,
            )

            for model_file in expected:
                (root / model_file).unlink(missing_ok=True)
            log.unlink()

            download_failed = subprocess.run(
                [sys.executable, str(scripts / DOWNLOAD_SCRIPT.name)],
                env=environment
                | {
                    "HF_FAKE_DENIED_SOURCE": denied_source_id,
                    "HF_FAKE_DENIED_STATUS": "500",
                },
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(download_failed.returncode, 0)
            for model_file in expected:
                (root / model_file).unlink(missing_ok=True)
            log.unlink()

            invalid = subprocess.run(
                [sys.executable, str(scripts / DOWNLOAD_SCRIPT.name)],
                env=environment | {"HF_TOKEN": "invalid"},
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(invalid.returncode, 0)
            self.assertEqual(
                invalid.stderr,
                "FAILED: the supplied Hugging Face token was rejected.\n",
            )
            self.assertFalse(log.exists())

            for failure in ("network", "server"):
                failed = subprocess.run(
                    [sys.executable, str(scripts / DOWNLOAD_SCRIPT.name)],
                    env=environment | {"HF_TOKEN": failure},
                    capture_output=True,
                    text=True,
                )
                self.assertNotEqual(failed.returncode, 0)
                self.assertFalse(log.exists())

            for source in expected_calls:
                self.assertIn(
                    f"Downloading {source['repo_id']}/{source['filename']} to ",
                    limited.stdout,
                )
            for model_file in local_model_files:
                self.assertIn(
                    f"Skipping {model_file}: no hfDownload source.\n",
                    limited.stdout,
                )

    def test_hf_token_is_available_only_to_the_model_build_step(self) -> None:
        """The credential is a build input, never runtime container state."""
        for dockerfile_name in ("Dockerfile", "Dockerfile.dev"):
            dockerfile = (REPO_ROOT / dockerfile_name).read_text()
            self.assertEqual(
                dockerfile.count(
                    "--mount=type=secret,id=huggingface_token,"
                    "env=HF_TOKEN"
                ),
                1,
            )
            self.assertEqual(dockerfile.count("ARG HF_DOWNLOAD_CACHEBUST"), 1)
            self.assertNotIn("required=true", dockerfile)
            self.assertNotIn("HF_TOKEN_PATH", dockerfile)

        compose_cache_keys = {
            "compose.yaml": 1,
            ".devcontainer/compose.devcont.yaml": 1,
            ".github/compose.ci.yaml": 2,
        }
        for compose_file, expected_count in compose_cache_keys.items():
            compose = (REPO_ROOT / compose_file).read_text()
            self.assertEqual(
                compose.count(
                    "HF_DOWNLOAD_CACHEBUST: ${HF_DOWNLOAD_CACHEBUST:-"
                    "${GITHUB_RUN_ID:-local}-${GITHUB_RUN_ATTEMPT:-0}}"
                ),
                expected_count,
            )

        for compose_file in (
            "compose.yaml",
            ".devcontainer/compose.devcont.yaml",
            ".devcontainer/docker-compose.rich.yaml",
        ):
            compose = (REPO_ROOT / compose_file).read_text()
            self.assertNotIn("HF_TOKEN_PATH", compose)
            self.assertNotIn("/run/secrets/huggingface_token", compose)

        platform_init = (REPO_ROOT / ".devcontainer/platform_init.sh").read_text()
        self.assertEqual(
            platform_init.count(
                'HF_DOWNLOAD_CACHEBUST "$(date +%s)-$$"'
            ),
            2,
        )

        workflow_secret_uses = {
            path.relative_to(REPO_ROOT).as_posix(): path.read_text().count(
                "secrets.HF_TOKEN"
            )
            for path in (REPO_ROOT / ".github" / "workflows").glob("*.yml")
            if "secrets.HF_TOKEN" in path.read_text()
        }
        self.assertEqual(
            workflow_secret_uses,
            {
                ".github/workflows/blackduck-scan.yml": 2,
                ".github/workflows/docker-scout-image-audit.yml": 1,
                ".github/workflows/pek-ci.yml": 3,
            },
        )
        docker_scout = (
            REPO_ROOT / ".github/workflows/docker-scout-image-audit.yml"
        ).read_text()
        self.assertIn("HF_TOKEN:\n        required: false", docker_scout)
        for path in (REPO_ROOT / ".github" / "workflows").glob("*.yml"):
            step = ""
            for line in path.read_text().splitlines():
                if line.lstrip().startswith("- name: "):
                    step = line.split("- name: ", 1)[1]
                if "secrets.HF_TOKEN" in line:
                    self.assertIn("Build", step)


if __name__ == "__main__":
    unittest.main()
