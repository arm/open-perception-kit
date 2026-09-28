#!/usr/bin/env python3
################################################################
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
################################################################

import argparse
import hashlib
import os
import platform
import subprocess
import sys
from pathlib import Path


INSTALL_INPUTS = [
    "install_executorch.sh",
    "install_executorch.py",
    "install_requirements.py",
    "install_utils.py",
    "pyproject.toml",
    "requirements-dev.txt",
    "requirements-examples.txt",
    "setup.py",
    "torch_pin.py",
    "version.txt",
]

LOCAL_PACKAGE_SUBMODULES = [
    "third-party/ao",
    "extension/llm/tokenizers",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compute the ExecuTorch Python installation cache state."
    )
    parser.add_argument("source", type=Path, help="ExecuTorch source directory")
    parser.add_argument("version", help="Expected ExecuTorch version")
    return parser.parse_args()


def add_value(digest, value: str) -> None:
    digest.update(value.encode())
    digest.update(b"\0")


def main() -> None:
    args = parse_args()
    digest = hashlib.sha256()

    for value in (
        "executorch-python-install-state-v1",
        args.version,
        sys.version,
        sys.executable,
        platform.platform(),
        os.environ.get("EXECUTORCH_BUILD_KERNELS_TORCHAO", ""),
        os.environ.get("USE_KINETO", ""),
    ):
        add_value(digest, value)

    for relative in INSTALL_INPUTS:
        path = args.source / relative
        add_value(digest, relative)
        digest.update(path.read_bytes())
        digest.update(b"\0")

    for relative in LOCAL_PACKAGE_SUBMODULES:
        revision = subprocess.check_output(
            ["git", "-C", str(args.source / relative), "rev-parse", "HEAD"],
            text=True,
        ).strip()
        add_value(digest, relative)
        add_value(digest, revision)

    print(digest.hexdigest())


if __name__ == "__main__":
    main()
