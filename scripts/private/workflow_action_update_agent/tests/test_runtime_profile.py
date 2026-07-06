################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import sys
from pathlib import Path
import unittest
import os
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tests'))
from agent_workflow_test_support import (  # noqa: E402
    HELPER_AGENTS_FILE,
    HELPER_RUNTIME,
    HELPER_STABILIZATION,
    PROFILE_FILE,
    REPO_ROOT,
    WORKFLOW_AUDIT_PROFILE_FILE,
    WORKFLOW_AUTOMATION_AGENTS_FILE,
)


class WorkflowActionUpdateAgentRuntimeProfileTests(unittest.TestCase):
    def test_workflow_action_update_agent_ownership_roots_have_agents_docs(self):
        helper_agents = HELPER_AGENTS_FILE.read_text(encoding="utf-8")
        workflow_agents = WORKFLOW_AUTOMATION_AGENTS_FILE.read_text(encoding="utf-8")

        self.assertIn("Workflow Action Update Agent helper commands", helper_agents)
        self.assertIn("scripts/private/github_api.py", helper_agents)
        self.assertIn("agent_runtime.github_actions", helper_agents)
        self.assertIn("prompt templates", workflow_agents)
        self.assertIn(".github/agent-runtime/runtime/agent-models.json", workflow_agents)
        self.assertIn("scripts/private/workflow_action_update_agent/", workflow_agents)

    def test_audit_profile_allows_non_failure_source_run_and_configures_validation(self):
        audit_profile = HELPER_RUNTIME.load_profile(str(WORKFLOW_AUDIT_PROFILE_FILE))

        self.assertFalse(HELPER_RUNTIME.profile_bool(audit_profile, "require_failure_conclusion", True))
        self.assertEqual(
            audit_profile["repair_branch_template"],
            "feature/{ticket_id}/bot-workflow-dependency-freshness-{source_run_id}",
        )
        self.assertEqual(audit_profile["repair_authorization_label"], "agent-autorepair")
        self.assertEqual(audit_profile["pr_trigger_label"], "run-pek-ci")
        audit_profile_source = WORKFLOW_AUDIT_PROFILE_FILE.read_text(encoding="utf-8")
        self.assertEqual(
            audit_profile["validation_workflows"],
            ["agent-review", "workflow-audit", "pek-ci", "sonar"],
        )
        self.assertEqual(audit_profile["validation_command_set"], "agent-workflow-python")
        self.assertNotIn("workflow_dispatch_inputs", audit_profile_source)
        validation_workflows = HELPER_RUNTIME.profile_validation_workflows(audit_profile)
        self.assertEqual(
            [item["workflow_file"] for item in validation_workflows],
            ["agent-review.yml", "workflow-audit.yml", "pek-ci.yml", "sonar.yml"],
        )

    def test_agent_review_gate_uses_canonical_validation_workflows(self):
        profile = HELPER_RUNTIME.load_profile(str(PROFILE_FILE))
        profile_source = PROFILE_FILE.read_text(encoding="utf-8")

        self.assertEqual(profile["validation_workflows"], ["agent-review", "pek-ci", "sonar"])
        self.assertEqual(profile["validation_command_set"], "agent-workflow-python")
        self.assertNotIn("workflow_dispatch_inputs", profile_source)
        self.assertNotIn("review_state_script", profile_source)
        validation_workflows = HELPER_RUNTIME.profile_validation_workflows(profile)
        agent_review = next(
            item for item in validation_workflows if item["workflow_file"] == "agent-review.yml"
        )
        pek_ci = next(
            item for item in validation_workflows if item["workflow_file"] == "pek-ci.yml"
        )
        sonar = next(
            item for item in validation_workflows if item["workflow_file"] == "sonar.yml"
        )

        self.assertEqual(agent_review["workflow_name"], "Agent Review")
        self.assertEqual(
            agent_review["review_state_script"],
            "scripts/private/agent_runtime/review/fetch.py",
        )
        self.assertEqual(agent_review["allowed_review_recommendations"], ["approve"])
        self.assertEqual(
            agent_review["workflow_dispatch_inputs"],
            {"base_ref": "origin/{target_branch}", "head_ref": "{repair_branch}"},
        )
        self.assertEqual(
            pek_ci["workflow_dispatch_inputs"],
            {
                "pr_number": "{pr_number}",
                "pr_base_ref": "{target_branch}",
                "pr_head_ref": "{repair_branch}",
                "pr_head_sha": "{head_sha}",
            },
        )
        self.assertEqual(pek_ci["workflow_dispatch_inputs"], sonar["workflow_dispatch_inputs"])
        self.assertNotIn("agent_model", profile)
        self.assertEqual(
            profile["agent_model_config"],
            ".github/agent-runtime/runtime/agent-models.json",
        )
        self.assertEqual(HELPER_RUNTIME.profile_config_root(str(PROFILE_FILE)), REPO_ROOT)
        self.assertEqual(
            HELPER_RUNTIME.profile_agent_model(profile, HELPER_RUNTIME.AgentInstance.REPAIR, str(PROFILE_FILE)),
            "gpt-5.5",
        )

    def test_validation_commands_strip_privileged_environment(self):
        env = {
            "PATH": "/usr/bin",
            "GITHUB_WORKSPACE": "/work",
            "GH_TOKEN": "x",
            "GITHUB_TOKEN": "x",
            "OPENAI_API_KEY": "x",
            "OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS": "x",
            "ACTIONS_RUNTIME_TOKEN": "x",
            "ACTIONS_ID_TOKEN_REQUEST_TOKEN": "x",
            "ACTIONS_ID_TOKEN_REQUEST_URL": "https://example.invalid",
            "GIT_ASKPASS": "/runner-private/askpass",
            "SSH_AUTH_SOCK": "/runner-private/ssh.sock",
            "GITHUB_ENV": "/runner-private/github-env",
            "GITHUB_OUTPUT": "/runner-private/github-output",
            "GITHUB_PATH": "/runner-private/github-path",
            "GITHUB_STEP_SUMMARY": "/runner-private/github-summary",
        }

        with mock.patch.dict(os.environ, env, clear=True):
            with mock.patch.object(HELPER_STABILIZATION, "run_validation_command") as run_validation_command:
                HELPER_STABILIZATION.run_validation_commands(
                    [
                        "python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_detect_secrets_quality_flow.py'",
                        "git diff --stat",
                    ]
                )

        self.assertEqual(
            [call.args[0] for call in run_validation_command.call_args_list],
            [
                "python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_detect_secrets_quality_flow.py'",
                "git diff --stat",
            ],
        )
        blocked_keys = set(env) - {"PATH", "GITHUB_WORKSPACE"}
        for call in run_validation_command.call_args_list:
            command_env = call.kwargs["env"]
            self.assertEqual(command_env["PATH"], "/usr/bin")
            self.assertEqual(command_env["GITHUB_WORKSPACE"], "/work")
            for key in blocked_keys:
                self.assertNotIn(key, command_env)

    def test_validation_commands_run_without_shell_and_reject_untrusted_commands(self):
        with mock.patch.object(HELPER_RUNTIME, "run_command") as run_command:
            HELPER_RUNTIME.run_validation_command(
                "python3 -m unittest discover -s tools/expkits-ci/tests -p 'test_agent_workflow_contracts.py'",
                env={"PATH": "/usr/bin"},
            )

        self.assertEqual(
            run_command.call_args.args[0],
            [
                "python3",
                "-m",
                "unittest",
                "discover",
                "-s",
                "tools/expkits-ci/tests",
                "-p",
                "test_agent_workflow_contracts.py",
            ],
        )
        self.assertEqual(run_command.call_args.kwargs["env"], {"PATH": "/usr/bin"})

        with self.assertRaisesRegex(ValueError, "trusted allowlist"):
            HELPER_RUNTIME.run_validation_command("python3 -c 'print(1)'")

    def test_profile_validation_commands_are_trusted_argv(self):
        profile = HELPER_RUNTIME.load_profile(str(PROFILE_FILE))

        self.assertEqual(
            [
                HELPER_RUNTIME.validation_command_args(command)
                for command in HELPER_RUNTIME.profile_validation_commands(profile)
            ],
            [
                [
                    "python3",
                    "-m",
                    "unittest",
                    "discover",
                    "-s",
                    "scripts/private/tests",
                ],
                [
                    "python3",
                    "-m",
                    "unittest",
                    "discover",
                    "-s",
                    "scripts/private/agent_runtime/tests",
                ],
                [
                    "python3",
                    "-m",
                    "unittest",
                    "discover",
                    "-s",
                    "scripts/private/workflow_action_update_agent/tests",
                ],
                [
                    "python3",
                    "-m",
                    "unittest",
                    "discover",
                    "-s",
                    "tools/expkits-ci/tests",
                    "-p",
                    "test_agent_static_analysis.py",
                ],
                [
                    "python3",
                    "-m",
                    "unittest",
                    "discover",
                    "-s",
                    "tools/expkits-ci/tests",
                    "-p",
                    "test_detect_secrets_quality_flow.py",
                ],
                [
                    "python3",
                    "-m",
                    "unittest",
                    "discover",
                    "-s",
                    "tools/expkits-ci/tests",
                    "-p",
                    "test_agent_workflow_contracts.py",
                ],
                ["git", "diff", "--stat"],
            ],
        )


if __name__ == "__main__":
    unittest.main()
