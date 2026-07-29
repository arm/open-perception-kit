#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Verify the host HF_TOKEN to container file-secret contract."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
MOUNT_NAME = "huggingface_token"
MOUNT_TARGET = f"/run/secrets/{MOUNT_NAME}"
CONTRACT_VALUE = "compose_contract_fixture_value"


class HuggingFaceAuthComposeTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("docker"), "Docker CLI unavailable")
    def test_runtime_services_use_the_same_file_secret(self):
        cases = (
            (("compose.yaml",), ("pek-dev",)),
            (
                (".devcontainer/compose.devcont.yaml",),
                ("pek-dev", "pek-dev-rpi5-h8", "pek-dev-rpi5-h10"),
            ),
            (
                (
                    ".devcontainer/compose.devcont.yaml",
                    ".devcontainer/docker-compose.rich.yaml",
                ),
                ("pek-dev-rich",),
            ),
        )
        for files, services in cases:
            configuration = self._compose_config(*files)
            for service in services:
                with self.subTest(files=files, service=service):
                    self._assert_secret_contract(configuration, service)

    @unittest.skipUnless(shutil.which("docker"), "Docker CLI unavailable")
    def test_ci_services_do_not_mount_the_host_secret(self):
        configuration = self._compose_config(".github/compose.ci.yaml")
        for name, service in configuration["services"].items():
            environment = service.get("environment", {})
            if "HF_TOKEN_PATH" not in environment:
                continue
            with self.subTest(service=name):
                self.assertNotIn("HF_TOKEN", environment)
                self.assertEqual(environment["HF_TOKEN_PATH"], MOUNT_TARGET)
                self.assertNotIn(
                    MOUNT_NAME,
                    {mount["source"] for mount in service.get("secrets", [])},
                )

    def test_platform_init_writes_only_blank_interpolation_fallbacks(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = Path(directory)
            (fixture / ".devcontainer").mkdir()
            scripts = fixture / "scripts/private"
            scripts.mkdir(parents=True)
            shutil.copy2(
                ROOT / ".devcontainer/platform_init.sh",
                fixture / ".devcontainer/platform_init.sh",
            )
            for name, body in (
                ("dev-init.sh", "exit 0\n"),
                ("select-webrtc-turn-mode.sh", "printf 'disabled\\n'\n"),
            ):
                script = scripts / name
                script.write_text(f"#!/usr/bin/env bash\n{body}", encoding="utf-8")
                script.chmod(0o755)

            environment = os.environ.copy()
            environment.pop("HF_TOKEN", None)
            subprocess.run(
                [fixture / ".devcontainer/platform_init.sh", "pek-dev", "disabled"],
                cwd=fixture,
                env=environment,
                check=True,
                capture_output=True,
                text=True,
            )

            for path in (fixture / ".env", fixture / ".devcontainer/.env"):
                self.assertIn("HF_TOKEN=\n", path.read_text(encoding="utf-8"))
            self.assertNotIn(
                "HF_TOKEN=",
                (fixture / "devices.env").read_text(encoding="utf-8"),
            )

    def _compose_config(self, *files):
        command = ["docker", "compose"]
        for path in files:
            command.extend(("-f", path))
        command.extend(("config", "--format", "json"))
        environment = os.environ | {
            "HOST_UID": "1000",
            "HOST_GID": "1000",
            "HF_TOKEN": CONTRACT_VALUE,
        }
        result = subprocess.run(
            command,
            cwd=ROOT,
            env=environment,
            check=True,
            capture_output=True,
            text=True,
        )
        self.assertNotIn(CONTRACT_VALUE, result.stdout + result.stderr)
        return json.loads(result.stdout)

    def _assert_secret_contract(self, configuration, service_name):
        self.assertEqual(
            configuration["secrets"][MOUNT_NAME]["environment"],
            "HF_TOKEN",
        )
        service = configuration["services"][service_name]
        self.assertNotIn("HF_TOKEN", service.get("environment", {}))
        self.assertEqual(service["environment"]["HF_TOKEN_PATH"], MOUNT_TARGET)
        mount = {item["source"]: item for item in service["secrets"]}[MOUNT_NAME]
        self.assertEqual(mount["target"], MOUNT_TARGET)
        self.assertEqual(int(str(mount["mode"]), 8), 0o400)


if __name__ == "__main__":
    unittest.main()
