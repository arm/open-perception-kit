#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

if __package__ in (None, ""):  # pragma: no cover - used for direct script execution.
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    __package__ = "agent_runtime.review"

from .prompt import render_prompt


def run_command(command: list[str]) -> None:
    subprocess.run(command, check=True)


def require_agent_python(agent_python: str) -> None:
    if shutil.which(agent_python) is None:
        raise SystemExit(f"Agent review requires Python 3.10 or newer; '{agent_python}' was not found.")

    version_check = (
        "import sys\n"
        "if sys.version_info < (3, 10):\n"
        "    raise SystemExit('.'.join(map(str, sys.version_info[:3])))\n"
    )
    completed = subprocess.run(
        [
            agent_python,
            "-c",
            version_check,
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode == 0:
        return
    version = (completed.stdout or completed.stderr).strip() or "unknown"
    raise SystemExit(f"Agent review requires Python 3.10 or newer; found Python {version}.")


def main() -> int:
    parser = argparse.ArgumentParser(description="Run Agent Review locally.")
    parser.add_argument("base_ref", nargs="?", default="origin/main")
    parser.add_argument("output_dir", nargs="?", default=".github/agent-runtime/review/out")
    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    os.environ["REVIEW_BASE_REF"] = args.base_ref
    os.environ.setdefault("REVIEW_HEAD_REF", "HEAD")
    os.environ.setdefault("REVIEW_REPOSITORY", "local-checkout")
    render_prompt(output_path=output_dir / "review.prompt.md")

    agent_venv = Path(os.environ.get("AGENT_REVIEW_AGENT_VENV", ".agent-runtime/openai-agent-venv"))
    agent_python = os.environ.get(
        "AGENT_REVIEW_PYTHON",
        os.environ.get("AGENT_RUNTIME_PYTHON", "python3"),
    )
    require_agent_python(agent_python)

    run_command([agent_python, "-m", "venv", str(agent_venv)])
    venv_python = agent_venv / "bin/python"
    run_command([str(venv_python), "-m", "pip", "install", "--upgrade", "pip"])
    run_command(
        [
            str(venv_python),
            "-m",
            "pip",
            "install",
            "-r",
            ".github/agent-runtime/runtime/requirements-openai-agents.txt",
        ]
    )

    agent_args = [
        str(venv_python),
        "scripts/private/agent_runtime/openai_agent_runner.py",
        "run-review",
        "--prompt-file",
        str(output_dir / "review.prompt.md"),
        "--schema-file",
        ".github/agent-runtime/review/schemas/review.schema.json",
        "--output-file",
        str(output_dir / "review.json"),
    ]
    if os.environ.get("AGENT_REVIEW_MODEL"):
        agent_args.extend(["--model", os.environ["AGENT_REVIEW_MODEL"]])
    run_command(agent_args)

    run_command(
        [
            str(venv_python),
            "scripts/private/agent_runtime/review/publish.py",
            "--input",
            str(output_dir / "review.json"),
            "--markdown-out",
            str(output_dir / "review-summary.md"),
        ]
    )

    print(f"Prompt: {output_dir / 'review.prompt.md'}")
    print(f"Review JSON: {output_dir / 'review.json'}")
    print(f"Review summary: {output_dir / 'review-summary.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
