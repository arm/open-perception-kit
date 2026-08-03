#!/usr/bin/env python3
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

"""Install the canonical generated Python SDK for local development."""

from __future__ import annotations

import argparse
import subprocess
import tempfile
from pathlib import Path

from artifacts import acquire_artifact
from sdk_config import load_sdk_config


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--python", required=True, help="target Python interpreter")
    parser.add_argument("--uv", default="uv", help="uv executable")
    parser.add_argument("--artifact-dir", type=Path)
    args = parser.parse_args()

    config = load_sdk_config()
    with tempfile.TemporaryDirectory(prefix="perception-dev-artifacts-") as tmp:
        wheel = acquire_artifact(
            config.flatbuffers_wheel,
            Path(tmp),
            cache_dir=args.artifact_dir,
        )
        subprocess.run(
            [
                args.uv,
                "pip",
                "install",
                "--python",
                args.python,
                "--no-deps",
                str(wheel),
                "--editable",
                str(config.python_project),
            ],
            check=True,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
