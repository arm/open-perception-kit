# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

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
            "tools/opk-ci/opk_ci/config_schema.py",
            "tools/opk-ci/tests/test_agent_static_analysis.py",
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
