#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


DEFAULT_VENV_PATH = ".agent-runtime/openai-agent-venv"
DEFAULT_REQUIREMENTS_FILE = ".github/agent-runtime/runtime/requirements-openai-agents.txt"


def run_command(args: list[str]) -> None:
    subprocess.run(args, check=True)


def venv_python(venv_path: Path) -> Path:
    return venv_path / "bin" / "python"


def setup_agent_runtime(
    *,
    venv_path: Path,
    requirements_file: Path,
    install_packages: list[str],
) -> None:
    run_command([sys.executable, "-m", "venv", str(venv_path)])
    python = venv_python(venv_path)
    run_command([str(python), "-m", "pip", "install", "--upgrade", "pip"])
    run_command([str(python), "-m", "pip", "install", "-r", str(requirements_file)])
    for package in install_packages:
        run_command([str(python), "-m", "pip", "install", package])


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Set up the OpenAI agent runtime virtual environment.")
    parser.add_argument("--venv-path", default=DEFAULT_VENV_PATH)
    parser.add_argument("--requirements-file", default=DEFAULT_REQUIREMENTS_FILE)
    parser.add_argument(
        "--install-package",
        action="append",
        default=[],
        help="Additional package spec to install into the runtime venv.",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    setup_agent_runtime(
        venv_path=Path(args.venv_path),
        requirements_file=Path(args.requirements_file),
        install_packages=list(args.install_package),
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
