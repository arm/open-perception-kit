#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import unittest
from unittest import mock


MODULE_PATH = Path(__file__).resolve().parents[1] / "setup_runtime.py"


def load_module():
    spec = importlib.util.spec_from_file_location("setup_runtime_under_test", MODULE_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load {MODULE_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


setup_runtime = load_module()


class SetupRuntimeTests(unittest.TestCase):
    def test_setup_agent_runtime_installs_requirements_and_extra_packages(self):
        with mock.patch.object(setup_runtime, "run_command") as run_command:
            setup_runtime.setup_agent_runtime(
                venv_path=Path(".agent-runtime/openai-agent-venv"),
                requirements_file=Path(".github/agent-runtime/runtime/requirements-openai-agents.txt"),
                install_packages=["./tools/expkits-ci"],
            )

        self.assertEqual(
            [call.args[0] for call in run_command.call_args_list],
            [
                [sys.executable, "-m", "venv", ".agent-runtime/openai-agent-venv"],
                [
                    ".agent-runtime/openai-agent-venv/bin/python",
                    "-m",
                    "pip",
                    "install",
                    "--upgrade",
                    "pip",
                ],
                [
                    ".agent-runtime/openai-agent-venv/bin/python",
                    "-m",
                    "pip",
                    "install",
                    "-r",
                    ".github/agent-runtime/runtime/requirements-openai-agents.txt",
                ],
                [
                    ".agent-runtime/openai-agent-venv/bin/python",
                    "-m",
                    "pip",
                    "install",
                    "./tools/expkits-ci",
                ],
            ],
        )


if __name__ == "__main__":
    unittest.main()
