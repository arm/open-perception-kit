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
import urllib.error

from github_api import github_api_query_endpoint, github_api_request


EXPECTED_REPOSITORY = "Arm-Debug/amp-dev-forge"
PACKAGE_NAME = "amp-dev-forge-ci"
ANCHOR_TAG = "retention-anchor"
SHA_PATTERN = re.compile(r"[0-9a-f]{40}")
TAG_PATTERN = re.compile(r"[0-9a-f]{40}(?:-[0-9]+-[0-9]+)?")
SERVICE_PATTERN = re.compile(r"[a-z0-9][a-z0-9_-]*")


def repository() -> str:
    value = os.environ.get("GITHUB_REPOSITORY", "").strip()
    if value.casefold() != EXPECTED_REPOSITORY.casefold():
        raise RuntimeError(f"GITHUB_REPOSITORY must be {EXPECTED_REPOSITORY}.")
    return value


def validate_sha(value: str) -> str:
    if not SHA_PATTERN.fullmatch(value):
        raise ValueError("CI image identity must be a full lowercase Git SHA.")
    return value


def validate_tag(value: str) -> str:
    if not TAG_PATTERN.fullmatch(value):
        raise ValueError("CI image tag must be a full lowercase Git SHA with an optional run identity.")
    return value


def image_ref(tag: str) -> str:
    return f"ghcr.io/{repository().lower()}-ci:{validate_tag(tag)}"


def run(command: list[str], *, check: bool = True, capture_output: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        check=check,
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


def prepare(sha: str, services: list[str], tag: str | None = None) -> str:
    sha = validate_sha(sha)
    if not services or any(not SERVICE_PATTERN.fullmatch(service) for service in services):
        raise ValueError("At least one valid Compose service is required.")

    image = image_ref(tag or sha)
    run(["docker", "pull", image])
    verify_revision(image, sha)
    project = compose_project_name()
    for service in services:
        run(["docker", "tag", image, f"{project}-{service}"])
    append_github_env("COMPOSE_PROJECT_NAME", project)
    append_github_env("PEK_CI_IMAGE", image)
    print(f"Prepared {image} for {', '.join(services)}")
    return image


def package_versions(owner: str) -> list[dict[str, object]] | None:
    versions: list[dict[str, object]] = []
    for page in range(1, 1000):
        endpoint = github_api_query_endpoint(
            f"orgs/{owner}/packages/container/{PACKAGE_NAME}/versions",
            {"per_page": 100, "page": page},
        )
        try:
            payload = json.loads(github_api_request(endpoint).decode("utf-8"))
        except urllib.error.HTTPError as error:
            error.close()
            if error.code == 404 and page == 1:
                return None
            raise
        if not isinstance(payload, list):
            raise RuntimeError("Unexpected GitHub Packages version payload.")
        page_versions = [version for version in payload if isinstance(version, dict)]
        versions.extend(page_versions)
        if len(payload) < 100:
            return versions
    raise RuntimeError("GitHub Packages pagination limit exceeded.")


def version_tags(version: dict[str, object]) -> list[str]:
    metadata = version.get("metadata")
    container = metadata.get("container") if isinstance(metadata, dict) else None
    tags = container.get("tags") if isinstance(container, dict) else None
    return [str(tag) for tag in tags] if isinstance(tags, list) else []


def cleanup(tag: str) -> None:
    tag = validate_tag(tag)
    owner = repository().split("/", 1)[0]
    versions = package_versions(owner)
    if versions is None:
        print(f"CI image package is already absent; nothing to delete for {tag}.")
        return

    matches = [version for version in versions if tag in version_tags(version)]
    if not matches:
        print(f"CI image tag is already absent: {tag}")
        return
    if len(matches) != 1:
        raise RuntimeError(f"Expected one CI image version for {tag}, found {len(matches)}.")

    package_path = f"orgs/{owner}/packages/container/{PACKAGE_NAME}"
    if not any(ANCHOR_TAG in version_tags(version) for version in versions):
        raise RuntimeError("CI image retention anchor is missing; refusing package-wide cleanup.")
    if ANCHOR_TAG in version_tags(matches[0]):
        raise RuntimeError("CI image tag and retention anchor unexpectedly share one version.")

    version_id = str(matches[0].get("id") or "")
    if not version_id.isdigit():
        raise RuntimeError(f"CI image version for {tag} has an invalid ID.")
    github_api_request(f"{package_path}/versions/{version_id}", method="DELETE")

    remaining = package_versions(owner) or []
    if any(tag in version_tags(version) for version in remaining):
        raise RuntimeError(f"CI image tag still exists after cleanup: {tag}")
    print(f"Deleted {PACKAGE_NAME}:{tag}.")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Consume and clean the shared PEK CI image.")
    subparsers = parser.add_subparsers(dest="command", required=True)
    for command in ("cleanup", "ref"):
        command_parser = subparsers.add_parser(command)
        command_parser.add_argument("tag")
    prepare_parser = subparsers.add_parser("prepare")
    prepare_parser.add_argument("sha")
    prepare_parser.add_argument("--tag")
    prepare_parser.add_argument("services", nargs="+")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.command == "prepare":
        prepare(args.sha, args.services, args.tag)
    elif args.command == "cleanup":
        cleanup(args.tag)
    else:
        print(image_ref(args.tag))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError, urllib.error.URLError) as error:
        print(f"CI image error: {error}", file=sys.stderr)
        raise SystemExit(1) from error
