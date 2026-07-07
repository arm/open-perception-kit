################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import inspect
import sys
from pathlib import Path
import unittest
import os
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tests'))
from agent_workflow_test_support import (  # noqa: E402
    HELPER_AGENTS_FILE,
    HELPER_PROFILE,
    HELPER_REVIEW_WORKFLOW,
    HELPER_STABILIZATION,
    HELPER_TASK_REFS,
    HELPER_VALIDATION,
    PROFILE_FILE,
    REPO_ROOT,
    WORKFLOW_AUDIT_PROFILE_FILE,
    WORKFLOW_AUTOMATION_AGENTS_FILE,
)


class WorkflowActionUpdateAgentProfileContractTests(unittest.TestCase):
    def test_workflow_action_update_agent_ownership_roots_have_agents_docs(self):
        helper_agents = HELPER_AGENTS_FILE.read_text(encoding="utf-8")
        workflow_agents = WORKFLOW_AUTOMATION_AGENTS_FILE.read_text(encoding="utf-8")

        self.assertIn("Workflow Action Update Agent helper commands", helper_agents)
        self.assertIn("scripts/private/github_api.py", helper_agents)
        self.assertIn("github_actions.py", helper_agents)
        self.assertIn("profile.py", helper_agents)
        self.assertIn("validation.py", helper_agents)
        self.assertIn("templates.py", helper_agents)
        self.assertNotIn("workflow_action_update_agent/runtime.py", helper_agents)
        self.assertIn("prompt templates", workflow_agents)
        self.assertIn(".github/agent-runtime/runtime/agent-models.json", workflow_agents)
        self.assertIn(".github/agent-runtime/runtime/agent-tasks.json", workflow_agents)
        self.assertIn("scripts/private/workflow_action_update_agent/", workflow_agents)
        self.assertIn("workflow_action_update_agent/profile.py", workflow_agents)
        self.assertIn("workflow_action_update_agent/validation.py", workflow_agents)
        self.assertNotIn("workflow_action_update_agent/runtime.py", workflow_agents)

    def test_audit_profile_allows_non_failure_source_run_and_configures_repair_label(self):
        audit_profile = HELPER_PROFILE.load_profile(str(WORKFLOW_AUDIT_PROFILE_FILE))

        self.assertFalse(HELPER_PROFILE.profile_bool(audit_profile, "require_failure_conclusion", True))
        self.assertEqual(
            audit_profile["repair_branch_template"],
            "feature/{task_ref}/bot-workflow-dependency-freshness-{source_run_id}",
        )
        self.assertEqual(audit_profile["repair_authorization_label"], "agent-repair")
        self.assertEqual(audit_profile["pr_trigger_label"], "run-pek-ci")
        audit_profile_source = WORKFLOW_AUDIT_PROFILE_FILE.read_text(encoding="utf-8")
        self.assertEqual(audit_profile["validation_command_set"], "agent-workflow-python")
        self.assertIn("standard PR validation", audit_profile["repair_definition_of_done"][-1])
        self.assertNotIn("agent-stabilize", audit_profile_source)
        self.assertNotIn("validation_workflows", audit_profile)
        self.assertNotIn("workflow_dispatch_inputs", audit_profile_source)
        self.assertNotIn("validation_workflows", audit_profile_source)

    def test_agent_review_state_shape_is_canonical_helper_contract(self):
        profile = HELPER_PROFILE.load_profile(str(PROFILE_FILE))
        profile_source = PROFILE_FILE.read_text(encoding="utf-8")
        agent_review = HELPER_REVIEW_WORKFLOW.standard_agent_review_workflow()

        self.assertEqual(profile["validation_command_set"], "agent-workflow-python")
        self.assertIn("standard PR validation", profile["repair_definition_of_done"][-1])
        self.assertNotIn("agent-stabilize", profile_source)
        self.assertNotIn("validation_workflows", profile)
        self.assertNotIn("validation_workflows", profile_source)
        self.assertNotIn("workflow_dispatch_inputs", profile_source)
        self.assertNotIn("review_state_script", profile_source)

        self.assertEqual(agent_review["workflow_name"], "Agent Review")
        self.assertEqual(
            agent_review["review_state_script"],
            "scripts/private/agent_runtime/review/fetch.py",
        )
        self.assertEqual(agent_review["workflow_file"], "agent-review.yml")
        self.assertNotIn("agent_model", profile)
        self.assertEqual(
            profile["agent_model_config"],
            ".github/agent-runtime/runtime/agent-models.json",
        )
        self.assertEqual(
            profile["agent_task_config"],
            ".github/agent-runtime/runtime/agent-tasks.json",
        )
        self.assertEqual(
            HELPER_PROFILE.profile_agent_model_config_file(profile, str(PROFILE_FILE)),
            ".github/agent-runtime/runtime/agent-models.json",
        )
        self.assertEqual(
            HELPER_PROFILE.profile_agent_task_config_file(profile, str(PROFILE_FILE)),
            ".github/agent-runtime/runtime/agent-tasks.json",
        )
        self.assertEqual(
            HELPER_PROFILE.profile_agent_model_config_file(
                profile,
                ".workflow-action-update-agent-helper/.github/agent-runtime/workflow-action-update-agent/profiles/profile.json",
            ),
            ".workflow-action-update-agent-helper/.github/agent-runtime/runtime/agent-models.json",
        )
        self.assertEqual(
            HELPER_PROFILE.profile_agent_task_config_file(
                profile,
                ".workflow-action-update-agent-helper/.github/agent-runtime/workflow-action-update-agent/profiles/profile.json",
            ),
            ".workflow-action-update-agent-helper/.github/agent-runtime/runtime/agent-tasks.json",
        )
        self.assertEqual(HELPER_PROFILE.profile_config_root(str(PROFILE_FILE)), REPO_ROOT)
        self.assertEqual(
            HELPER_PROFILE.profile_agent_model(profile, HELPER_PROFILE.AgentInstance.REPAIR, str(PROFILE_FILE)),
            "gpt-5.5",
        )
        HELPER_PROFILE.profile_agent_task_settings(
            profile,
            HELPER_PROFILE.AgentCommand.REPAIR,
            str(PROFILE_FILE),
        )
        self.assertEqual(
            HELPER_PROFILE.profile_agent_runtime_config_outputs(
                profile,
                command=HELPER_PROFILE.AgentCommand.REPAIR,
                profile_path=str(PROFILE_FILE),
            ),
            {
                "agent_model_config_file": ".github/agent-runtime/runtime/agent-models.json",
                "agent_task_config_file": ".github/agent-runtime/runtime/agent-tasks.json",
            },
        )
        self.assertNotIn(
            "agent_instance",
            inspect.signature(HELPER_PROFILE.profile_agent_runtime_config_outputs).parameters,
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

    def test_task_ref_resolution_accepts_single_ref_and_rejects_missing_or_conflicting_refs(self):
        self.assertEqual(
            HELPER_TASK_REFS.resolve_task_ref(
                "feature/TASK-1/update-workflow-agent",
                purpose="test",
            ),
            "TASK-1",
        )
        with self.assertRaisesRegex(ValueError, "requires a task reference"):
            HELPER_TASK_REFS.resolve_task_ref("feature/no-reference", purpose="test")
        with self.assertRaisesRegex(ValueError, "conflicting task references"):
            HELPER_TASK_REFS.resolve_task_ref(
                "feature/TASK-1/update-workflow-agent",
                "TASK-2: different title",
                purpose="test",
            )

    def test_validation_commands_run_without_shell_and_reject_untrusted_commands(self):
        with mock.patch.object(HELPER_VALIDATION, "run_command") as run_command:
            HELPER_VALIDATION.run_validation_command(
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
            HELPER_VALIDATION.run_validation_command("python3 -c 'print(1)'")

    def test_profile_validation_commands_are_trusted_argv(self):
        profile = HELPER_PROFILE.load_profile(str(PROFILE_FILE))

        self.assertEqual(
            [
                HELPER_VALIDATION.validation_command_args(command)
                for command in HELPER_PROFILE.profile_validation_commands(profile)
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
