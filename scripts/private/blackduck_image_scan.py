#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Run stock Detect image scans without its optional symlink-following rebuild."""

import argparse
import io
import json
import math
import os
from pathlib import Path
import re
import subprocess
import tarfile
import time


# Leave 15 minutes beyond Detect's server wait for inspection and diagnostics.
IMAGE_TIMEOUT = 4500
DETECT_TIMEOUT = 3600


def inspected_filesystem(output, log):
    """Detect 12 can return success after Docker Inspector returned 255."""
    if re.search(r"Process return code: (?!0\b)-?\d+", log):
        raise RuntimeError("An image-scan subprocess failed despite Detect's result")
    if "Error inspecting image:" in log:
        raise RuntimeError("Docker Inspector failed")
    filesystems = list(output.glob("runs/*/extractions/DOCKER-*/*_containerfilesystem.tar.gz"))
    inventories = list(output.glob("runs/*/extractions/DOCKER-*/*bdio.jsonld"))
    if len(filesystems) != 1 or not inventories:
        raise RuntimeError("Expected one container filesystem and a native Docker package inventory")
    if any(path.stat().st_size == 0 for path in [*filesystems, *inventories]):
        raise RuntimeError("Empty image scan input")
    return filesystems[0]


def verify_image_scan(output, log):
    filesystem = inspected_filesystem(output, log)
    commands = [line for line in log.splitlines() if "Black Duck CLI command:" in line]
    if not commands or any(str(filesystem) not in line for line in commands):
        raise RuntimeError("Signature scanner did not exclusively target the container filesystem")
    statuses = re.findall(r"Overall Status: (\w+)", log)
    if not statuses or any(status != "SUCCESS" for status in statuses):
        raise RuntimeError("Detect did not report successful completion")
    return filesystem


def archive_diagnostics(root, destination):
    """Keep logs/native reports, including colon names, but not extracted images."""
    if not root.is_dir():
        print(f"No scan diagnostics at {root}")
        return
    secret = os.environ.get("BLACKDUCK_TOKEN", "").encode()
    # S5042 flags every tarfile.open call; this creates output and never extracts archive input.
    with tarfile.open(destination, "w:gz") as archive:  # NOSONAR(S5042)
        for directory, directories, filenames in os.walk(root, followlinks=False):
            directories[:] = sorted(name for name in directories if name not in {
                "containerFileSystem", "input", "squashedImageBuildDir", "tools",
            })
            for name in sorted(filenames):
                path = Path(directory) / name
                if path.is_symlink() or not (name.endswith((".log", ".bdio", "bdio.jsonld")) or name in {
                    "status.json", "results.json", "scan-input.json", "verification.json",
                }):
                    continue
                data = path.read_bytes()
                if secret:
                    data = data.replace(secret, b"***")
                info = archive.gettarinfo(str(path), arcname=str(path.relative_to(root)))
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))
    print(f"Diagnostics: {destination}")


def scan_image(args):
    token = os.environ["BLACKDUCK_TOKEN"]
    url = os.environ["BLACKDUCK_URL"]
    repository = os.environ["GITHUB_REPOSITORY"]
    if not token or not url or not re.fullmatch(r"[0-9a-f]{40}", args.source_sha):
        raise ValueError("Token, server URL and exact source SHA are required")
    image = json.loads(subprocess.check_output(["docker", "image", "inspect", args.image]))[0]
    if (image["Config"].get("Labels") or {}).get("org.opencontainers.image.revision") != args.source_sha:
        raise ValueError("Image revision does not match the selected source")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    fallback = output / "empty-source"
    fallback.mkdir()
    version = args.version.replace("/", "-")
    project = repository.replace("/", ":")
    (output / "scan-input.json").write_text(json.dumps({
        "source_sha": args.source_sha, "image": args.image, "image_id": image["Id"],
        "architecture": image["Architecture"], "project": project, "version": version,
        "label": args.label,
    }, indent=2) + "\n")
    common = [
        f"--blackduck.url={url}", f"--blackduck.api.token={token}",
        f"--detect.project.name={project}", f"--detect.project.version.name={version}",
        f"--detect.code.location.name={project}/{version}/{args.label}",
        "--detect.cleanup=false", f"--detect.source.path={fallback}",
        "--detect.policy.check.fail.on.severities=ALL",
    ]
    deadline = time.monotonic() + IMAGE_TIMEOUT
    scan_log = output / "scan.log"
    with scan_log.open("w") as log:
        for phase in ("inventory", "signature"):
            remaining = math.ceil(deadline - time.monotonic())
            if remaining <= 30:
                raise RuntimeError("Image scan exhausted its shared time budget")
            if phase == "inventory":
                options = [
                    "--detect.target.type=IMAGE", "--detect.tools=DOCKER",
                    f"--detect.docker.image.id={image['Id']}",
                    # Avoid SquashedImage.java's recursive traversal of Debian symlinks.
                    "--detect.docker.passthrough.output.include.squashedimage=false",
                    "--detect.docker.passthrough.output.include.containerfilesystem=true",
                    f"--detect.output.path={output}",
                ]
            else:
                log.flush()
                filesystem = inspected_filesystem(output, scan_log.read_text())
                # Detect 12 does not automatically select the unsquashed filesystem.
                options = [
                    "--detect.tools=SIGNATURE_SCAN",
                    f"--detect.blackduck.signature.scanner.paths={filesystem}",
                    f"--detect.output.path={output / 'signature'}",
                ]
            command = [
                "timeout", "--kill-after=30", str(remaining), "bash", str(args.wrapper.resolve()),
                *common, *options, f"--detect.timeout={min(DETECT_TIMEOUT, remaining - 30)}",
            ]
            with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True) as process:
                for line in process.stdout:
                    line = line.replace(token, "***")
                    print(line, end="", flush=True)
                    log.write(line)
                status = process.wait()
            if status:
                raise RuntimeError(f"Image {phase} scan exited {status}; see retained diagnostics")
    filesystem = verify_image_scan(output, scan_log.read_text())
    (output / "verification.json").write_text(json.dumps({
        "status": "SUCCESS", "source_sha": args.source_sha,
        "image_id": image["Id"], "signature_input": str(filesystem),
    }, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    image = commands.add_parser("image")
    image.add_argument("--wrapper", type=Path, required=True)
    image.add_argument("--image", required=True)
    image.add_argument("--source-sha", required=True)
    image.add_argument("--version", required=True)
    image.add_argument("--label", required=True)
    image.add_argument("--output", type=Path, required=True)
    archive = commands.add_parser("archive")
    archive.add_argument("root", type=Path)
    archive.add_argument("destination", type=Path)
    args = parser.parse_args()
    if args.command == "image":
        scan_image(args)
    else:
        archive_diagnostics(args.root, args.destination)


if __name__ == "__main__":
    main()
