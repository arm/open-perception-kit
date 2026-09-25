#!/usr/bin/env python3
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

"""Unified open-perception-kit development and release command."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parent


def run_script(name: str, arguments: list[str]) -> int:
    return subprocess.run(
        [sys.executable, str(TOOLS_DIR / name), *arguments],
        check=False,
    ).returncode


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="./scripts/perception-sdk.sh",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    commands = {
        "generate": "regenerate the canonical SDK snapshot",
        "check": "regenerate in a temporary directory and check for drift",
        "package": "build the reproducible SDK release bundle",
        "verify": "verify a bundle directory or release ZIP",
        "install-dev": "install the generated Python SDK for development",
    }
    parser.add_argument("command", nargs="?", choices=commands, help="SDK command")
    if not argv or argv[0] in {"-h", "--help"}:
        parser.epilog = (
            "commands:\n"
            + "\n".join(
                f"  {command:<12} {help_text}" for command, help_text in commands.items()
            )
            + "\n\n"
            "Run './scripts/perception-sdk.sh <command> --help' for command options.\n\n"
            "examples:\n"
            "  ./scripts/perception-sdk.sh check\n"
            "  ./scripts/perception-sdk.sh package --expect-version MAJOR.MINOR.PATCH\n"
            "  ./scripts/perception-sdk.sh verify "
            "artifacts/open-perception-kit-MAJOR.MINOR.PATCH.zip "
            "--require-sidecars"
        )
        parser.print_help()
        raise SystemExit(0)
    if argv[0] not in commands:
        parser.error(f"invalid command: {argv[0]}")
    return argparse.Namespace(command=argv[0], arguments=argv[1:])


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.command == "generate":
        return run_script("generate.py", args.arguments)
    if args.command == "check":
        return run_script("generate.py", ["--check", *args.arguments])
    if args.command == "package":
        return run_script("package.py", ["package", *args.arguments])
    if args.command == "verify":
        return run_script("package.py", ["verify", *args.arguments])
    return run_script("install_dev.py", args.arguments)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
