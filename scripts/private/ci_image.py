#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys


SHA_PATTERN = re.compile(r"[0-9a-f]{40}")
SERVICE_PATTERN = re.compile(r"[a-z0-9][a-z0-9_-]*")


def validate_sha(value: str) -> str:
    if not SHA_PATTERN.fullmatch(value):
        raise ValueError("CI image identity must be a full lowercase Git SHA.")
    return value


def image_ref(sha: str) -> str:
    return f"pek-ci:{validate_sha(sha)}"


def run(command: list[str], *, capture_output: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        check=True,
        text=True,
        stdout=subprocess.PIPE if capture_output else None,
        stderr=subprocess.PIPE if capture_output else None,
    )


def image_label(image: str, label: str) -> str:
    result = run(
        [
            "docker",
            "image",
            "inspect",
            "--format",
            f"{{{{ index .Config.Labels {json.dumps(label)} }}}}",
            image,
        ],
        capture_output=True,
    )
    return result.stdout.strip()


def verify_revision(image: str, sha: str) -> None:
    revision = image_label(image, "org.opencontainers.image.revision")
    if revision != sha:
        raise RuntimeError(f"CI image revision is {revision or '<missing>'}, expected {sha}.")


def compose_project_name() -> str:
    value = os.environ.get("COMPOSE_PROJECT_NAME", "").strip()
    if not value:
        parts = (
            os.environ.get("GITHUB_RUN_ID", ""),
            os.environ.get("GITHUB_RUN_ATTEMPT", ""),
            os.environ.get("GITHUB_JOB", ""),
        )
        if not all(parts):
            raise RuntimeError("COMPOSE_PROJECT_NAME or the GitHub run identity is required.")
        value = f"pek-{parts[0]}-{parts[1]}-{parts[2]}"
    if not SERVICE_PATTERN.fullmatch(value):
        raise ValueError(f"Unsupported Compose project name: {value}")
    return value


def append_github_env(name: str, value: str) -> None:
    path = os.environ.get("GITHUB_ENV", "").strip()
    if not path:
        return
    with Path(path).open("a", encoding="utf-8") as env_file:
        env_file.write(f"{name}={value}\n")


def prepare(sha: str, archive: str, services: list[str]) -> str:
    sha = validate_sha(sha)
    if not services or any(not SERVICE_PATTERN.fullmatch(service) for service in services):
        raise ValueError("At least one valid Compose service is required.")

    image = image_ref(sha)
    run(["docker", "image", "load", "--input", archive])
    Path(archive).unlink()
    verify_revision(image, sha)
    project = compose_project_name()
    for service in services:
        run(["docker", "tag", image, f"{project}-{service}"])
    append_github_env("COMPOSE_PROJECT_NAME", project)
    append_github_env("PEK_CI_IMAGE", image)
    print(f"Prepared {image} for {', '.join(services)}")
    return image


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Load the shared PEK CI image.")
    subparsers = parser.add_subparsers(dest="command", required=True)
    prepare_parser = subparsers.add_parser("prepare")
    prepare_parser.add_argument("sha")
    prepare_parser.add_argument("--archive", required=True)
    prepare_parser.add_argument("services", nargs="+")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    prepare(args.sha, args.archive, args.services)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        print(f"CI image error: {error}", file=sys.stderr)
        raise SystemExit(1) from error
