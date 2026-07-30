################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import importlib.util
from pathlib import Path
import unittest


def load_agent_static_analysis_module():
    module_path = Path(__file__).resolve().parents[1] / "expkits_ci" / "agent_static_analysis.py"
    spec = importlib.util.spec_from_file_location("agent_static_analysis_under_test", module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load {module_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


agent_static_analysis = load_agent_static_analysis_module()


class AgentStaticAnalysisTests(unittest.TestCase):
    def test_derives_removed_paths_from_name_status(self):
        name_status = "\0".join(
            [
                "R100",
                "tools/retired-runner.py",
                "tools/agent-runner.py",
                "D",
                "scripts/private/unused_helper.py",
                "",
            ]
        )

        self.assertEqual(
            agent_static_analysis.parse_removed_or_renamed_paths(name_status),
            ["tools/retired-runner.py", "scripts/private/unused_helper.py"],
        )

    def test_reference_tokens_include_suffixless_path(self):
        self.assertEqual(
            agent_static_analysis.reference_tokens_for_removed_path("scripts/private/old-helper.py"),
            {"scripts/private/old-helper.py", "scripts/private/old-helper"},
        )

    def test_ignore_rules_are_not_source_references(self):
        self.assertNotIn(".gitignore", agent_static_analysis.AGENT_STATIC_REFERENCE_PATHS)


if __name__ == "__main__":
    unittest.main()
