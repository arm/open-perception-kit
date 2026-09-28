#!/usr/bin/env python3
################################################################
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
################################################################

import argparse
import importlib
from importlib import metadata


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate the installed ExecuTorch Python package."
    )
    parser.add_argument("version", help="Expected ExecuTorch version")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    importlib.import_module("executorch")
    importlib.import_module("torch")
    installed = metadata.version("executorch").split("+", 1)[0]
    if installed != args.version:
        raise SystemExit(
            f"installed ExecuTorch version {installed} does not match {args.version}"
        )


if __name__ == "__main__":
    main()
