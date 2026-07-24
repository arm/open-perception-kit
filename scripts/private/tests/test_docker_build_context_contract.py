#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Protect credentials and package staging data at the Docker build boundary."""

from __future__ import annotations

from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[3]
DOCKERIGNORE = REPO_ROOT / ".dockerignore"
DOCKERFILE = REPO_ROOT / "Dockerfile"
DEVCONTAINER_COMPOSE = REPO_ROOT / ".devcontainer/compose.devcont.yaml"
RICH_COMPOSE = REPO_ROOT / ".devcontainer/docker-compose.rich.yaml"
DEPLOYMENT_COMPOSE = REPO_ROOT / "compose.yaml"
EXECUTORCH_INSTALLER = (
    REPO_ROOT / "scripts/private/executorch/install-executorch-deb.sh"
)


class DockerBuildContextContractTests(unittest.TestCase):
    def test_all_container_entrypoints_select_current_executorch_revision(self) -> None:
        self.assertEqual(
            DOCKERFILE.read_text(encoding="utf-8").count(
                "ARG EXECUTORCH_DEB_REVISION=2"
            ),
            2,
        )
        for path, expected_count in (
            (DEVCONTAINER_COMPOSE, 4),
            (RICH_COMPOSE, 1),
            (DEPLOYMENT_COMPOSE, 1),
        ):
            with self.subTest(path=path):
                self.assertEqual(
                    path.read_text(encoding="utf-8").count(
                        "EXECUTORCH_DEB_REVISION: "
                        "${EXECUTORCH_DEB_REVISION:-2}"
                    ),
                    expected_count,
                )

    def test_host_environment_and_device_files_are_excluded(self) -> None:
        entries = {
            line.strip()
            for line in DOCKERIGNORE.read_text(encoding="utf-8").splitlines()
            if line.strip() and not line.lstrip().startswith("#")
        }

        self.assertTrue(
            {
                ".env",
                "devices.env",
                ".devcontainer/.env",
                ".devcontainer/devices.env",
                ".devcontainer/cameras.env",
                ".devcontainer/*.video.yaml",
                ".devcontainer/*.audio.yaml",
                ".devcontainer/*.npu.yaml",
                ".devcontainer/*.shared_memory.yaml",
            }.issubset(entries)
        )

    def test_package_is_available_to_fetch_stage_but_excluded_from_deployment(self) -> None:
        dockerignore = DOCKERIGNORE.read_text(encoding="utf-8")
        dockerfile = DOCKERFILE.read_text(encoding="utf-8")

        self.assertIn("!var/libexecutorch-dev-*.deb", dockerignore.splitlines())
        self.assertIn(
            "COPY --exclude=var/libexecutorch-dev-*.deb "
            "--chown=${USERNAME}:${USERNAME} . /work",
            dockerfile.splitlines(),
        )

    def test_artifactory_fetch_uses_the_checked_in_signing_key(self) -> None:
        dockerfile = DOCKERFILE.read_text(encoding="utf-8")
        installer = EXECUTORCH_INSTALLER.read_text(encoding="utf-8")

        self.assertIn(
            "--mount=type=bind,"
            "source=scripts/private/executorch/artifactory-debian-public.asc,"
            "target=/tmp/artifactory-debian-public.asc,ro",
            dockerfile,
        )
        # Artifactory authenticity is a current security boundary: package
        # bytes must be rooted in the checked-in key, never an APT trust bypass.
        self.assertNotIn("trusted=yes", installer)


if __name__ == "__main__":
    unittest.main()
