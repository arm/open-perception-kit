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
REGISTRY_IMAGE_PATTERN = re.compile(
    r"ghcr\.io/[a-z0-9][a-z0-9._/-]*:pek-ci-run-[0-9]+-[0-9]+"
)
DEV_IMAGE_TAG_PATTERN = re.compile(r"sha-[0-9a-f]{40}")
SERVICE_PATTERN = re.compile(r"[a-z0-9][a-z0-9_-]*")
DEV_IMAGE_INPUTS = (
    "Dockerfile",
    ".dockerignore",
    "compose.base.yaml",
    "config",
    ".devcontainer/compose.devcont.yaml",
    ".devcontainer/configs/zshrc",
    "development/web/package-lock.json",
    "generated/perception/python",
    "scripts/download-models.py",
    "scripts/private/demo-videos.manifest",
    "scripts/private/development-entrypoint.sh",
    "scripts/private/download-demo-videos.sh",
    "scripts/private/executorch/install-executorch-deb.sh",
    "scripts/private/generate-hf-download-cachebust.sh",
    "scripts/private/install-onnxruntime.sh",
    "scripts/private/install-perception-flatbuffers.sh",
    "tools/expkits-ci",
    "tools/perception/sdk.json",
    "tools/plumber",
    "var",
)


def validate_sha(value: str) -> str:
    if not SHA_PATTERN.fullmatch(value):
        raise ValueError("CI image identity must be a full lowercase Git SHA.")
    return value


def github_repository() -> str:
    value = os.environ.get("GITHUB_REPOSITORY", "").strip().lower()
    if value.count("/") != 1:
        raise RuntimeError("GITHUB_REPOSITORY must contain owner/repository.")
    return value


def dev_repository() -> str:
    return f"ghcr.io/{github_repository()}-dev"


def dev_image_ref(sha: str) -> str:
    return f"{dev_repository()}:sha-{validate_sha(sha)}"


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


def append_github_output(name: str, value: str) -> None:
    path = os.environ.get("GITHUB_OUTPUT", "").strip()
    if not path:
        return
    with Path(path).open("a", encoding="utf-8") as output_file:
        output_file.write(f"{name}={value}\n")


def prepare(sha: str, registry_image: str, services: list[str]) -> str:
    sha = validate_sha(sha)
    if not services or any(not SERVICE_PATTERN.fullmatch(service) for service in services):
        raise ValueError("At least one valid Compose service is required.")
    if not REGISTRY_IMAGE_PATTERN.fullmatch(registry_image):
        raise ValueError("Registry CI image must be a run-tagged lowercase GHCR reference.")

    run(["docker", "pull", registry_image])
    verify_revision(registry_image, sha)
    project = compose_project_name()
    for service in services:
        run(["docker", "tag", registry_image, f"{project}-{service}"])
    append_github_env("COMPOSE_PROJECT_NAME", project)
    append_github_env("PEK_CI_IMAGE", registry_image)
    print(f"Prepared {registry_image} for {', '.join(services)}")
    return registry_image


def git_head_sha() -> str:
    result = run(["git", "rev-parse", "HEAD"], capture_output=True)
    return validate_sha(result.stdout.strip())


def dev_inputs_unchanged(base_sha: str, head_sha: str) -> bool:
    try:
        run(["git", "diff", "--quiet", base_sha, head_sha, "--", *DEV_IMAGE_INPUTS])
    except subprocess.CalledProcessError as error:
        if error.returncode == 1:
            return False
        raise
    return True


def compatible_dev_sha(head_sha: str) -> str:
    result = run(
        ["git", "log", "-1", "--format=%H", head_sha, "--", *DEV_IMAGE_INPUTS],
        capture_output=True,
    )
    return validate_sha(result.stdout.strip())


def pull_dev_image(sha: str) -> str | None:
    image = dev_image_ref(sha)
    try:
        run(["docker", "pull", image])
    except subprocess.CalledProcessError:
        return None
    verify_revision(image, sha)
    return image


def prepare_dev(base_sha: str) -> str | None:
    base_sha = validate_sha(base_sha)
    head_sha = git_head_sha()
    candidates = [head_sha]
    if base_sha != head_sha and dev_inputs_unchanged(base_sha, head_sha):
        candidates.append(base_sha)
    candidates.append(compatible_dev_sha(head_sha))

    image = None
    for candidate in dict.fromkeys(candidates):
        image = pull_dev_image(candidate)
        if image is not None:
            break

    if image is None:
        print("::warning::Prebuilt image unavailable; falling back to the QEMU build.")
    else:
        run(["docker", "tag", image, f"{compose_project_name()}-pek-dev"])

    append_github_output("start_args", "--no-build" if image is not None else "")
    return image


def version_tags(version: dict[str, object]) -> list[str]:
    metadata = version.get("metadata", {})
    if not isinstance(metadata, dict):
        raise ValueError("Package version metadata must be an object.")
    container = metadata.get("container", {})
    if not isinstance(container, dict):
        raise ValueError("Package version container metadata must be an object.")
    tags = container.get("tags", [])
    if not isinstance(tags, list) or any(not isinstance(tag, str) for tag in tags):
        raise ValueError("Package version tags must be strings.")
    return tags


def versions_to_delete(versions: list[dict[str, object]], keep: int) -> list[int]:
    if keep < 0:
        raise ValueError("Retention count cannot be negative.")
    sha_versions = sorted(
        (
            version
            for version in versions
            if any(
                DEV_IMAGE_TAG_PATTERN.fullmatch(tag)
                for tag in version_tags(version)
            )
        ),
        key=lambda version: str(version.get("created_at", "")),
        reverse=True,
    )
    expired_ids = {version.get("id") for version in sha_versions[keep:]}
    deletion_ids: list[int] = []
    for version in versions:
        version_id = version.get("id")
        if not isinstance(version_id, int):
            raise ValueError("Package version id must be an integer.")
        tags = version_tags(version)
        if not tags or (
            version_id in expired_ids
            and all(DEV_IMAGE_TAG_PATTERN.fullmatch(tag) for tag in tags)
        ):
            deletion_ids.append(version_id)
    return deletion_ids


def retain_dev_images(keep: int) -> list[int]:
    owner, repository = github_repository().split("/", 1)
    package = f"{repository}-dev"
    endpoint = f"orgs/{owner}/packages/container/{package}/versions"
    result = run(
        ["gh", "api", "--paginate", "--jq", ".[]", f"{endpoint}?per_page=100"],
        capture_output=True,
    )
    versions = [json.loads(line) for line in result.stdout.splitlines()]
    if any(not isinstance(version, dict) for version in versions):
        raise ValueError("GitHub package versions response contains an invalid version.")
    deletion_ids = versions_to_delete(versions, keep)
    for version_id in deletion_ids:
        run(["gh", "api", "--method", "DELETE", f"{endpoint}/{version_id}"])
    print(f"Deleted {len(deletion_ids)} superseded {package} version(s).")
    return deletion_ids


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Load the shared PEK CI image.")
    subparsers = parser.add_subparsers(dest="command", required=True)
    prepare_parser = subparsers.add_parser("prepare")
    prepare_parser.add_argument("sha")
    prepare_parser.add_argument("--registry-image", required=True)
    prepare_parser.add_argument("services", nargs="+")
    prepare_dev_parser = subparsers.add_parser("prepare-dev")
    prepare_dev_parser.add_argument("--base-sha", required=True)
    retain_dev_parser = subparsers.add_parser("retain-dev")
    retain_dev_parser.add_argument("--keep", type=int, default=20)
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.command == "prepare":
        prepare(args.sha, args.registry_image, args.services)
    elif args.command == "prepare-dev":
        prepare_dev(args.base_sha)
    else:
        retain_dev_images(args.keep)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        print(f"CI image error: {error}", file=sys.stderr)
        raise SystemExit(1) from error
