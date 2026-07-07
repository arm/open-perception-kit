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

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from test_support.agent_workflow import (  # noqa: E402
    COMMON_PROFILE_SCRIPT,
    COMMON_WORKFLOW_AGENTS_FILE,
    REPAIR_HELPER_AGENTS_FILE,
    REPAIR_PROFILE,
    REPAIR_PROFILE_SCRIPT,
    COMMON_REVIEW_WORKFLOW,
    STABILIZATION_PROFILE,
    STABILIZATION_PROFILE_SCRIPT,
    COMMON_TASK_REFS,
    COMMON_VALIDATION,
    SOURCE_RUN_REPAIR_PROFILE_FILE,
    REPO_ROOT,
    STABILIZATION_ORCHESTRATOR_AGENTS_FILE,
    STABILIZATION_PROFILE_FILE,
    WORKFLOW_DEPENDENCY_FRESHNESS_PROFILE_FILE,
    SOURCE_RUN_REPAIR_AGENTS_FILE,
)


class AgentWorkflowProfileContractTests(unittest.TestCase):
    def test_agentic_workflow_ownership_roots_have_agents_docs(self):
        helper_agents = REPAIR_HELPER_AGENTS_FILE.read_text(encoding="utf-8")
        stabilization_agents = STABILIZATION_ORCHESTRATOR_AGENTS_FILE.read_text(encoding="utf-8")
        common_agents = COMMON_WORKFLOW_AGENTS_FILE.read_text(encoding="utf-8")
        workflow_agents = SOURCE_RUN_REPAIR_AGENTS_FILE.read_text(encoding="utf-8")

        self.assertIn("source-run repair helper commands", helper_agents)
        self.assertIn("scripts/private/agent_workflow_common/", helper_agents)
        self.assertIn("current-PR stabilization", stabilization_agents)
        self.assertIn("helper snapshots under `.agent-runtime/agent-stabilization-helper/`", stabilization_agents)
        self.assertIn("shared helper code", common_agents)
        self.assertIn("cross-flow helpers", common_agents)
        self.assertIn("profile runtime-config helpers", common_agents)
        self.assertIn("source-run repair profiles and prompt templates", workflow_agents)
        self.assertIn(".github/agent-runtime/runtime/agent-models.json", workflow_agents)
        self.assertIn(".github/agent-runtime/runtime/agent-tasks.json", workflow_agents)
        self.assertNotIn(".github/agent-runtime/pr-stabilization/profiles", workflow_agents)

    def test_profile_runtime_helpers_are_common_but_schemas_stay_flow_specific(self):
        common_profile_source = COMMON_PROFILE_SCRIPT.read_text(encoding="utf-8")
        repair_profile_source = REPAIR_PROFILE_SCRIPT.read_text(encoding="utf-8")
        stabilization_profile_source = STABILIZATION_PROFILE_SCRIPT.read_text(encoding="utf-8")

        self.assertIn("def profile_agent_runtime_config_outputs", common_profile_source)
        self.assertIn("def validate_profile_schema", common_profile_source)
        self.assertNotIn("resolve_agent_model", repair_profile_source)
        self.assertNotIn("resolve_agent_task_settings", repair_profile_source)
        self.assertNotIn("resolve_agent_model", stabilization_profile_source)
        self.assertNotIn("resolve_agent_task_settings", stabilization_profile_source)
        self.assertIn("repair_branch_template", repair_profile_source)
        self.assertNotIn("repair_branch_template", stabilization_profile_source)

    def test_audit_profile_allows_non_failure_source_run_and_configures_repair_label(self):
        audit_profile = REPAIR_PROFILE.load_profile(str(WORKFLOW_DEPENDENCY_FRESHNESS_PROFILE_FILE))

        self.assertFalse(REPAIR_PROFILE.profile_bool(audit_profile, "require_failure_conclusion", True))
        self.assertEqual(
            audit_profile["repair_branch_template"],
            "feature/{task_ref}/bot-workflow-dependency-freshness-{source_run_id}",
        )
        self.assertEqual(audit_profile["repair_authorization_label"], "agent-repair")
        self.assertEqual(audit_profile["pr_trigger_label"], "run-pek-ci")
        audit_profile_source = WORKFLOW_DEPENDENCY_FRESHNESS_PROFILE_FILE.read_text(encoding="utf-8")
        self.assertEqual(audit_profile["validation_command_set"], "agent-workflow-python")
        self.assertIn("standard PR validation", audit_profile["repair_definition_of_done"][-1])
        self.assertNotIn("agent-stabilize", audit_profile_source)
        self.assertNotIn("validation_workflows", audit_profile)
        self.assertNotIn("workflow_dispatch_inputs", audit_profile_source)
        self.assertNotIn("validation_workflows", audit_profile_source)

    def test_agent_review_state_shape_is_canonical_helper_contract(self):
        profile = REPAIR_PROFILE.load_profile(str(SOURCE_RUN_REPAIR_PROFILE_FILE))
        profile_source = SOURCE_RUN_REPAIR_PROFILE_FILE.read_text(encoding="utf-8")
        agent_review = COMMON_REVIEW_WORKFLOW.standard_agent_review_workflow()

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
            REPAIR_PROFILE.profile_agent_model_config_file(profile, str(SOURCE_RUN_REPAIR_PROFILE_FILE)),
            ".github/agent-runtime/runtime/agent-models.json",
        )
        self.assertEqual(
            REPAIR_PROFILE.profile_agent_task_config_file(profile, str(SOURCE_RUN_REPAIR_PROFILE_FILE)),
            ".github/agent-runtime/runtime/agent-tasks.json",
        )
        self.assertEqual(
            REPAIR_PROFILE.profile_agent_model_config_file(
                profile,
                ".agent-runtime/agent-stabilization-helper/.github/agent-runtime/source-run-repair/profiles/profile.json",
            ),
            ".agent-runtime/agent-stabilization-helper/.github/agent-runtime/runtime/agent-models.json",
        )
        self.assertEqual(
            REPAIR_PROFILE.profile_agent_task_config_file(
                profile,
                ".agent-runtime/agent-stabilization-helper/.github/agent-runtime/source-run-repair/profiles/profile.json",
            ),
            ".agent-runtime/agent-stabilization-helper/.github/agent-runtime/runtime/agent-tasks.json",
        )
        self.assertEqual(REPAIR_PROFILE.profile_config_root(str(SOURCE_RUN_REPAIR_PROFILE_FILE)), REPO_ROOT)
        self.assertEqual(
            REPAIR_PROFILE.profile_agent_model(
                profile, REPAIR_PROFILE.AgentInstance.REPAIR, str(SOURCE_RUN_REPAIR_PROFILE_FILE)),
            "gpt-5.5",
        )
        REPAIR_PROFILE.profile_agent_task_settings(
            profile,
            REPAIR_PROFILE.AgentCommand.REPAIR,
            str(SOURCE_RUN_REPAIR_PROFILE_FILE),
        )
        self.assertEqual(
            REPAIR_PROFILE.profile_agent_runtime_config_outputs(
                profile,
                command=REPAIR_PROFILE.AgentCommand.REPAIR,
                profile_path=str(SOURCE_RUN_REPAIR_PROFILE_FILE),
            ),
            {
                "agent_model_config_file": ".github/agent-runtime/runtime/agent-models.json",
                "agent_task_config_file": ".github/agent-runtime/runtime/agent-tasks.json",
            },
        )
        self.assertNotIn(
            "agent_instance",
            inspect.signature(REPAIR_PROFILE.profile_agent_runtime_config_outputs).parameters,
        )

    def test_stabilization_profile_schema_is_separate_from_repair_metadata(self):
        profile = STABILIZATION_PROFILE.load_profile(str(STABILIZATION_PROFILE_FILE))
        profile_source = STABILIZATION_PROFILE_FILE.read_text(encoding="utf-8")

        self.assertEqual(profile["display_name"], "Agent PR Stabilization")
        self.assertEqual(profile["validation_command_set"], "agent-workflow-python")
        self.assertIn(".github/agent-runtime/pr-stabilization/prompts/context.md", profile["prompt_context_files"])
        self.assertNotIn("repair_branch_template", profile)
        self.assertNotIn("repair_authorization_label", profile)
        self.assertNotIn("pr_description_template", profile_source)
        self.assertEqual(
            STABILIZATION_PROFILE.profile_agent_model_config_file(profile, str(STABILIZATION_PROFILE_FILE)),
            ".github/agent-runtime/runtime/agent-models.json",
        )
        self.assertEqual(
            STABILIZATION_PROFILE.profile_agent_task_config_file(profile, str(STABILIZATION_PROFILE_FILE)),
            ".github/agent-runtime/runtime/agent-tasks.json",
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
            with mock.patch.object(COMMON_VALIDATION, "run_validation_command") as run_validation_command:
                COMMON_VALIDATION.run_validation_commands(
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
            COMMON_TASK_REFS.resolve_task_ref(
                "feature/TASK-1/update-workflow-agent",
                purpose="test",
            ),
            "TASK-1",
        )
        with self.assertRaisesRegex(ValueError, "requires a task reference"):
            COMMON_TASK_REFS.resolve_task_ref("feature/no-reference", purpose="test")
        with self.assertRaisesRegex(ValueError, "conflicting task references"):
            COMMON_TASK_REFS.resolve_task_ref(
                "feature/TASK-1/update-workflow-agent",
                "TASK-2: different title",
                purpose="test",
            )

    def test_validation_commands_run_without_shell_and_reject_untrusted_commands(self):
        with mock.patch.object(COMMON_VALIDATION, "run_command") as run_command:
            COMMON_VALIDATION.run_validation_command(
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
            COMMON_VALIDATION.run_validation_command("python3 -c 'print(1)'")

    def test_profile_validation_commands_are_trusted_argv(self):
        profile = REPAIR_PROFILE.load_profile(str(SOURCE_RUN_REPAIR_PROFILE_FILE))

        self.assertEqual(
            [
                COMMON_VALIDATION.validation_command_args(command)
                for command in REPAIR_PROFILE.profile_validation_commands(profile)
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
                    "scripts/private/agent_repair_orchestrator/tests",
                ],
                [
                    "python3",
                    "-m",
                    "unittest",
                    "discover",
                    "-s",
                    "scripts/private/agent_stabilization_orchestrator/tests",
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
