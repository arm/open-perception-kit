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


class SdkHelpFormatter(
    argparse.ArgumentDefaultsHelpFormatter,
    argparse.RawDescriptionHelpFormatter,
):
    def _get_help_string(self, action: argparse.Action) -> str:
        if action.default in {None, False, argparse.SUPPRESS} or action.required:
            return action.help
        return super()._get_help_string(action)


def main() -> int:
    parser = argparse.ArgumentParser(
        prog="./scripts/perception-sdk.sh install-dev",
        description=__doc__,
        formatter_class=SdkHelpFormatter,
        epilog=(
            "example:\n"
            "  ./scripts/perception-sdk.sh install-dev "
            "--python tools/.venv/bin/python"
        ),
    )
    parser.add_argument(
        "--python",
        required=True,
        metavar="EXECUTABLE",
        help="target Python interpreter or virtual environment receiving the SDK",
    )
    parser.add_argument(
        "--uv",
        default="uv",
        metavar="EXECUTABLE",
        help="uv executable used for the editable installation",
    )
    parser.add_argument(
        "--artifact-dir",
        type=Path,
        metavar="PATH",
        help=(
            "read-write cache for the checksum-locked FlatBuffers wheel; missing or "
            "invalid artifacts are downloaded"
        ),
    )
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
