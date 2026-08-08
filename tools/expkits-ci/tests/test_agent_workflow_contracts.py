################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "scripts/private"))
from test_support.agent_workflow import load_quality_checks_module  # noqa: E402


class AgentWorkflowBehaviorTests(unittest.TestCase):
    def test_agent_runtime_static_analysis_trigger_paths(self):
        quality_checks = load_quality_checks_module()

        for path in (
            "scripts/download-models.py",
            "scripts/private/agent_repair_orchestrator/cli.py",
            "scripts/private/agent_stabilization_orchestrator/cli.py",
            "scripts/private/agent_workflow_common/validation.py",
            "scripts/private/agent_runtime/openai_agent_runner.py",
            "scripts/private/github_actions.py",
            "scripts/private/tests/test_github_api.py",
            "scripts/private/test_support/agent_workflow.py",
            "tools/expkits-ci/tests/test_agent_static_analysis.py",
        ):
            with self.subTest(path=path):
                self.assertTrue(
                    quality_checks.QualityChecks.should_run_agent_runtime_static_analysis(
                        [path]
                    )
                )

        self.assertFalse(
            quality_checks.QualityChecks.should_run_agent_runtime_static_analysis(
                ["scripts/private/unrelated_helper.py"]
            )
        )


if __name__ == "__main__":
    unittest.main()
