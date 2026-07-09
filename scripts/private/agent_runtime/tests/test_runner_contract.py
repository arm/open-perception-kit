################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import asyncio
import os
import sys
from pathlib import Path
import unittest
import json
import tempfile
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from test_support.agent_workflow import (  # noqa: E402
    AGENT_MODEL_CONFIG_FILE,
    AGENT_REQUIREMENTS_FILE,
    AGENT_REVIEW_CONTEXT,
    AGENT_REVIEW_CONTEXT_SCRIPT,
    AGENT_REVIEW_FETCH_SCRIPT,
    AGENT_REVIEW_INSTRUCTIONS_FILE,
    AGENT_REVIEW_PUBLISH_SCRIPT,
    AGENT_REVIEW_ROOT,
    AGENT_TASK_CONFIG_FILE,
    OPENAI_AGENT_CONTRACTS,
    OPENAI_AGENT_CONTRACTS_SCRIPT,
    OPENAI_AGENT_MODEL_CONFIG,
    OPENAI_AGENT_MODEL_CONFIG_SCRIPT,
    OPENAI_AGENT_RUNTIME_CONTEXT,
    OPENAI_AGENT_PATH_TOOLS_SCRIPT,
    OPENAI_AGENT_REPO_TOOLS_SCRIPT,
    OPENAI_AGENT_RUNNER_SCRIPT,
    OPENAI_AGENT_SDK_RUNTIME_SCRIPT,
    OPENAI_AGENT_SHELL_TOOLS_SCRIPT,
    OPENAI_AGENT_TASKS_SCRIPT,
    OPENAI_AGENT_TASK_CONFIG,
    OPENAI_AGENT_TASK_CONFIG_SCRIPT,
    OPENAI_AGENT_TASK_ESTIMATOR_SCRIPT,
    OPENAI_AGENT_WORKFLOW_TASK_SCRIPT,
    OPENAI_PATCH_MAX_TURNS,
    OPENAI_REVIEW_MAX_TURNS,
    REPO_ROOT,
    load_agent_workflow_module_with_fake_sdk,
)


class AgentRuntimeContractTests(unittest.TestCase):
    def test_openai_agent_runner_uses_arm_proxy_truststore_and_tracing_contract(self):
        runner_source = OPENAI_AGENT_RUNNER_SCRIPT.read_text(encoding="utf-8")
        sdk_source = OPENAI_AGENT_SDK_RUNTIME_SCRIPT.read_text(encoding="utf-8")
        workflow_task_source = OPENAI_AGENT_WORKFLOW_TASK_SCRIPT.read_text(encoding="utf-8")
        task_source = OPENAI_AGENT_TASKS_SCRIPT.read_text(encoding="utf-8")
        tools_source = OPENAI_AGENT_REPO_TOOLS_SCRIPT.read_text(encoding="utf-8")
        shell_tools_source = OPENAI_AGENT_SHELL_TOOLS_SCRIPT.read_text(encoding="utf-8")
        path_tools_source = OPENAI_AGENT_PATH_TOOLS_SCRIPT.read_text(encoding="utf-8")
        estimator_source = OPENAI_AGENT_TASK_ESTIMATOR_SCRIPT.read_text(encoding="utf-8")
        task_config_source = OPENAI_AGENT_TASK_CONFIG_SCRIPT.read_text(encoding="utf-8")
        model_config_source = OPENAI_AGENT_MODEL_CONFIG_SCRIPT.read_text(encoding="utf-8")
        contracts_source = OPENAI_AGENT_CONTRACTS_SCRIPT.read_text(encoding="utf-8")

        self.assertIn(
            'DEFAULT_OPENAI_BASE_URL = "https://openai-api-proxy.geo.arm.com/api/providers/openai-eu/v1"',
            contracts_source,
        )
        self.assertEqual(
            OPENAI_AGENT_CONTRACTS.DEFAULT_AGENT_MODEL_CONFIG_PATH,
            ".github/agent-runtime/runtime/agent-models.json",
        )
        self.assertEqual(
            OPENAI_AGENT_CONTRACTS.DEFAULT_AGENT_TASK_CONFIG_PATH,
            ".github/agent-runtime/runtime/agent-tasks.json",
        )
        self.assertEqual(OPENAI_AGENT_CONTRACTS.OPENAI_AGENTS_DISABLE_TRACING_VALUE, "1")
        self.assertEqual(OPENAI_AGENT_CONTRACTS.OPENAI_API_KEY_ENV, "OPENAI_API_KEY")
        self.assertEqual(
            OPENAI_AGENT_CONTRACTS.OPENAI_PROXY_KEY_ENV,
            "OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS",
        )
        self.assertIn("ReviewRecommendation", task_source)
        self.assertIn("ReviewSeverity", task_source)
        self.assertIn("DiffSide", task_source)
        python_guard = sdk_source.index("require_supported_python()")
        truststore_import = sdk_source.index("import truststore")
        inject_call = sdk_source.index("truststore.inject_into_ssl()")
        agents_import = sdk_source.index("from agents import")
        self.assertIn("MIN_AGENT_RUNTIME_PYTHON = (3, 10)", sdk_source)
        self.assertLess(python_guard, truststore_import)
        self.assertLess(truststore_import, agents_import)
        self.assertLess(inject_call, agents_import)
        self.assertIn("def configure_openai_environment", sdk_source)
        self.assertIn("RunContextWrapper", sdk_source)
        self.assertIn("class AgentWorkflowTask", workflow_task_source)
        self.assertIn("class ConfiguredAgentWorkflowTask", task_source)
        self.assertIn("class ReviewAgentTask", task_source)
        self.assertEqual(task_source.count("class RepositoryEditAgentTask"), 1)
        self.assertIn("from .base import AgentWorkflowTask", task_source)
        self.assertIn("class ReviewResult", task_source)
        self.assertIn("return ReviewResult", task_source)
        self.assertIn('Reasoning(effort="high")', task_source)
        self.assertNotIn("temperature=", task_source)
        self.assertNotIn("verbosity=", task_source)
        self.assertIn("REVIEW_AGENT_INPUT", task_source)
        self.assertIn("context=review_context", task_source)
        self.assertIn("filter_invalid_right_side_findings", task_source)
        self.assertIn("verified_model=args.resolved_model", task_source)
        self.assertIn("import shlex", shell_tools_source)
        self.assertIn("def split_shell_commands", shell_tools_source)
        self.assertIn("def run_parsed_shell_command", shell_tools_source)
        self.assertIn("def find_subcommand", shell_tools_source)
        self.assertIn("READ_ONLY_GIT_SUBCOMMANDS", shell_tools_source)
        self.assertIn("FORBIDDEN_GIT_OPTIONS", shell_tools_source)
        self.assertIn("def is_allowed_git_command", shell_tools_source)
        self.assertIn("def has_forbidden_git_option", shell_tools_source)
        self.assertIn("def build_subprocess_environment", shell_tools_source)
        self.assertIn("env=environment", shell_tools_source)
        self.assertNotIn("shell=True", shell_tools_source)
        self.assertIn('"apply"', shell_tools_source)
        self.assertIn("def validate_patch_paths", path_tools_source)
        self.assertIn('["git", "apply", "--whitespace=nowarn"]', tools_source)
        self.assertIn("async def estimate_task_fit", estimator_source)
        self.assertIn("def build_task_manifest", estimator_source)
        self.assertIn("def deterministic_task_limit_violations", estimator_source)
        self.assertNotIn("TaskEstimatorWorkflowTask", estimator_source)
        self.assertNotIn("run_agent(", estimator_source)
        self.assertIn("class AgentTaskSettings", task_config_source)
        self.assertIn("def resolve_agent_task_settings", task_config_source)
        self.assertIn("from ..contracts import", model_config_source)
        self.assertIn("from ..contracts import", task_config_source)
        self.assertNotIn("def require_non_empty_string", model_config_source)
        self.assertNotIn("def require_non_empty_string", task_config_source)
        self.assertNotIn("def parse_agent_instance", model_config_source)
        self.assertNotIn("def parse_agent_instance", task_config_source)
        self.assertNotIn("def parse_agent_command", task_config_source)
        self.assertFalse((OPENAI_AGENT_RUNNER_SCRIPT.parent / "task_registry.py").exists())
        self.assertIn("get_agent_task", runner_source)
        self.assertIn("validate_agent_task_registry", runner_source)
        self.assertIn("resolve_task_settings", runner_source)
        self.assertNotIn("--agent-instance", runner_source)
        self.assertNotIn("agent_instance_override", runner_source)
        self.assertIn("--model-config-file", runner_source)
        self.assertIn("--task-config-file", runner_source)
        self.assertNotRegex(runner_source, r"(^|\s)--model(\s|=|$)")
        self.assertNotIn("--override-model", model_config_source)
        self.assertNotIn("override_model", model_config_source)
        self.assertNotIn("--task-estimate-turns", runner_source)
        self.assertNotIn("ReviewResult", runner_source)
        self.assertNotIn("read_repo_file", runner_source)
        self.assertNotIn("TaskEstimate", runner_source)
        self.assertNotIn("AGENT_TASK_LIMITS", contracts_source)

    def test_agent_task_registry_is_enforced_against_central_config(self):
        runner = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_RUNNER_SCRIPT,
            "agent_runtime.openai_agent_runner_fake_sdk_registry",
        )
        configured_commands = set(
            json.loads(AGENT_TASK_CONFIG_FILE.read_text(encoding="utf-8"))["tasks"]
        )
        registered_commands = {task.command.value for task in runner.iter_agent_tasks()}

        self.assertEqual(registered_commands, configured_commands)
        edit_tasks = [
            task
            for task in runner.iter_agent_tasks()
            if task.command in {OPENAI_AGENT_CONTRACTS.AgentCommand.REPAIR, OPENAI_AGENT_CONTRACTS.AgentCommand.STABILIZATION}
        ]
        self.assertEqual({type(task).__name__ for task in edit_tasks}, {"RepositoryEditAgentTask"})
        for task in edit_tasks:
            self.assertFalse(hasattr(task.build_agent(model="gpt-test"), "model_settings"))
        runner.validate_agent_task_registry(AGENT_TASK_CONFIG_FILE)

        with tempfile.TemporaryDirectory() as temp_dir:
            config_path = Path(temp_dir) / "agent-tasks.json"
            payload = json.loads(AGENT_TASK_CONFIG_FILE.read_text(encoding="utf-8"))
            del payload["tasks"]["run-repair"]
            config_path.write_text(json.dumps(payload), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Agent task registry and config do not match"):
                runner.validate_agent_task_registry(config_path)

        original_tasks = runner.AGENT_TASKS
        try:
            runner.AGENT_TASKS = (original_tasks[0], original_tasks[0], *original_tasks[1:])
            with self.assertRaisesRegex(ValueError, "duplicate commands"):
                runner.validate_agent_task_registry(AGENT_TASK_CONFIG_FILE)
        finally:
            runner.AGENT_TASKS = original_tasks

    def test_task_estimator_uses_deterministic_preflight_only(self):
        estimator = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_TASK_ESTIMATOR_SCRIPT,
            "agent_runtime.tasks.estimator_fake_contract",
        )
        agent_tasks = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_TASKS_SCRIPT,
            "agent_runtime.tasks.configured_fake_contract",
        )

        self.assertFalse(hasattr(estimator, "TaskEstimatorWorkflowTask"))
        self.assertFalse(hasattr(estimator, "TaskEstimate"))
        self.assertTrue(issubclass(agent_tasks.ConfiguredAgentWorkflowTask, agent_tasks.AgentWorkflowTask))
        self.assertTrue(issubclass(agent_tasks.ReviewAgentTask, agent_tasks.AgentWorkflowTask))
        self.assertTrue(issubclass(agent_tasks.RepositoryEditAgentTask, agent_tasks.AgentWorkflowTask))

    def test_review_agent_configuration_is_static_typed_and_structured(self):
        agent_tasks = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_TASKS_SCRIPT,
            "agent_runtime.tasks.configured_fake_sdk_agent",
        )
        OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(REPO_ROOT, 30)

        task = agent_tasks.ReviewAgentTask()
        agent = task.build_agent(model="gpt-test")

        self.assertEqual(agent.name, "Pull request reviewer")
        self.assertEqual(agent.instructions, AGENT_REVIEW_INSTRUCTIONS_FILE.read_text(encoding="utf-8"))
        self.assertEqual(agent.model, "gpt-test")
        self.assertEqual(agent.model_settings.reasoning.effort, "high")
        self.assertIsNone(agent.model_settings.temperature)
        self.assertIsNone(agent.model_settings.verbosity)
        self.assertEqual(
            [tool.__name__ for tool in agent.tools],
            ["get_review_context", "read_repo_file", "list_repo_files", "run_shell_command"],
        )
        self.assertIs(agent.output_type, agent_tasks.ReviewResult)
        self.assertNotIn("runtime-title-marker", agent.instructions)

    def test_shared_sdk_runner_receives_typed_review_context(self):
        agent_tasks = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_TASKS_SCRIPT,
            "agent_runtime.tasks.configured_fake_sdk_runner_context",
        )
        context = AGENT_REVIEW_CONTEXT.ReviewRunContext(
            repo_root=REPO_ROOT,
            command_timeout=30,
            repository="Arm-Debug/amp-dev-forge",
            base_ref="origin/develop",
            head_ref="HEAD",
            base_sha="a" * 40,
            head_sha="b" * 40,
            pull_request=AGENT_REVIEW_CONTEXT.PullRequestEvidence(
                number=101,
                title="Safe title",
                body=None,
                url=None,
            ),
            limits=AGENT_REVIEW_CONTEXT.ReviewLimits(
                max_review_files=120,
                max_review_changed_lines=15000,
                max_pr_title_chars=AGENT_REVIEW_CONTEXT.MAX_PR_TITLE_CHARS,
                max_pr_body_chars=AGENT_REVIEW_CONTEXT.MAX_PR_BODY_CHARS,
                max_pr_url_chars=AGENT_REVIEW_CONTEXT.MAX_PR_URL_CHARS,
            ),
            completeness=AGENT_REVIEW_CONTEXT.ReviewCompleteness(
                pull_request_available=True,
                pr_title_truncated=False,
                pr_body_original_chars=0,
                pr_body_normalized_chars=0,
                pr_body_truncated=False,
                pr_url_truncated=False,
            ),
        )
        OPENAI_AGENT_RUNTIME_CONTEXT.activate_run_context(context)
        task = agent_tasks.ReviewAgentTask()
        runner = task.run_agent.__func__.__globals__["Runner"]
        runner_result = mock.Mock(final_output="typed-result")

        with mock.patch.object(
            runner,
            "run",
            new=mock.AsyncMock(return_value=runner_result),
        ) as sdk_run:
            result = asyncio.run(
                task.run_agent(
                    agent_tasks.REVIEW_AGENT_INPUT,
                    model="gpt-test",
                    max_turns=OPENAI_REVIEW_MAX_TURNS,
                    context=context,
                )
            )

        self.assertEqual(result, "typed-result")
        sdk_run.assert_awaited_once()
        call = sdk_run.await_args
        self.assertIsNotNone(call)
        assert call is not None
        self.assertEqual(call.args[1], agent_tasks.REVIEW_AGENT_INPUT)
        self.assertIs(call.kwargs["context"], context)
        self.assertEqual(call.kwargs["max_turns"], OPENAI_REVIEW_MAX_TURNS)
        self.assertTrue(call.kwargs["run_config"].tracing_disabled)

    def test_review_task_passes_constant_input_and_typed_context_to_runner(self):
        agent_tasks = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_TASKS_SCRIPT,
            "agent_runtime.tasks.configured_fake_sdk_context_run",
        )
        settings = OPENAI_AGENT_TASK_CONFIG.resolve_agent_task_settings(
            AGENT_TASK_CONFIG_FILE,
            OPENAI_AGENT_CONTRACTS.AgentCommand.REVIEW,
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir) / "repo"
            repo_root.mkdir()
            payload = AGENT_REVIEW_CONTEXT.build_review_context_payload(
                repo_root=repo_root,
                task_config_path=AGENT_TASK_CONFIG_FILE,
                environment={
                    "REVIEW_BASE_SHA": "a" * 40,
                    "REVIEW_HEAD_SHA": "b" * 40,
                    "REVIEW_PR_NUMBER": "101",
                    "REVIEW_PR_TITLE": "runtime-title-marker",
                    "REVIEW_PR_BODY": "Arbitrary Unicode body: 🧪",
                },
            )
            context_file = repo_root / ".github/agent-runtime/review/out/review-context.json"
            context_file.parent.mkdir(parents=True)
            context_file.write_text(json.dumps(payload), encoding="utf-8")
            event_file = Path(temp_dir) / "event.json"
            event_contents = '{"pull_request":{"body":"raw-pr-body-marker"}}\n'
            event_file.write_text(event_contents, encoding="utf-8")
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(repo_root, 30)
            args = argparse.Namespace(
                context_file=str(context_file),
                task_settings=settings,
                resolved_model="gpt-test",
                output_file=str(Path(temp_dir) / "review.json"),
            )
            task = agent_tasks.ReviewAgentTask()

            def capture_run(*_args: object, **_kwargs: object) -> str:
                self.assertFalse(context_file.exists())
                self.assertFalse(event_file.exists())
                return "result"

            with mock.patch.dict(os.environ, {"GITHUB_EVENT_PATH": str(event_file)}), mock.patch.object(
                    agent_tasks,
                    "estimate_task_fit",
                    new=mock.AsyncMock(),
            ), mock.patch.object(
                    task,
                    "run_agent",
                    new=mock.AsyncMock(side_effect=capture_run),
            ) as run_agent, mock.patch.object(task, "write_result", return_value=0):
                result = asyncio.run(task.run(args))
            self.assertTrue(context_file.exists())
            self.assertIn("Arbitrary Unicode body: 🧪", context_file.read_text(encoding="utf-8"))
            self.assertEqual(event_file.read_text(encoding="utf-8"), event_contents)

        self.assertEqual(result, 0)
        run_agent.assert_awaited_once()
        call = run_agent.await_args
        self.assertIsNotNone(call)
        assert call is not None
        self.assertEqual(call.args, (agent_tasks.REVIEW_AGENT_INPUT,))
        self.assertEqual(call.kwargs["model"], "gpt-test")
        self.assertEqual(call.kwargs["max_turns"], OPENAI_REVIEW_MAX_TURNS)
        self.assertIsInstance(call.kwargs["context"], AGENT_REVIEW_CONTEXT.ReviewRunContext)
        self.assertEqual(call.kwargs["context"].pull_request.title, "runtime-title-marker")
        self.assertNotIn("runtime-title-marker", AGENT_REVIEW_INSTRUCTIONS_FILE.read_text(encoding="utf-8"))

    def test_get_review_context_returns_structured_serializable_evidence(self):
        agent_tasks = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_TASKS_SCRIPT,
            "agent_runtime.tasks.configured_fake_sdk_context_tool",
        )
        context = AGENT_REVIEW_CONTEXT.ReviewRunContext(
            repo_root=REPO_ROOT,
            command_timeout=30,
            repository="Arm-Debug/amp-dev-forge",
            base_ref="origin/develop",
            head_ref="HEAD",
            base_sha="a" * 40,
            head_sha="b" * 40,
            pull_request=AGENT_REVIEW_CONTEXT.PullRequestEvidence(
                number=101,
                title="Safe title",
                body="Keep the typed context boundary, regardless of description format.",
                url="https://github.com/Arm-Debug/amp-dev-forge/pull/101",
            ),
            limits=AGENT_REVIEW_CONTEXT.ReviewLimits(
                max_review_files=120,
                max_review_changed_lines=15000,
                max_pr_title_chars=AGENT_REVIEW_CONTEXT.MAX_PR_TITLE_CHARS,
                max_pr_body_chars=AGENT_REVIEW_CONTEXT.MAX_PR_BODY_CHARS,
                max_pr_url_chars=AGENT_REVIEW_CONTEXT.MAX_PR_URL_CHARS,
            ),
            completeness=AGENT_REVIEW_CONTEXT.ReviewCompleteness(
                pull_request_available=True,
                pr_title_truncated=False,
                pr_body_original_chars=66,
                pr_body_normalized_chars=66,
                pr_body_truncated=False,
                pr_url_truncated=False,
            ),
        )

        payload = agent_tasks.get_review_context(argparse.Namespace(context=context))

        json.dumps(payload)
        self.assertEqual(payload["review_scope"]["base_sha"], "a" * 40)
        evidence = payload["untrusted_pull_request_evidence"]
        self.assertEqual(evidence["title"], "Safe title")
        self.assertEqual(
            evidence["body"],
            "Keep the typed context boundary, regardless of description format.",
        )
        self.assertIn("untrusted pull-request author content", evidence["trust_boundary"])

    def test_invalid_review_context_fails_before_agent_invocation(self):
        agent_tasks = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_TASKS_SCRIPT,
            "agent_runtime.tasks.configured_fake_sdk_invalid_context",
        )
        settings = OPENAI_AGENT_TASK_CONFIG.resolve_agent_task_settings(
            AGENT_TASK_CONFIG_FILE,
            OPENAI_AGENT_CONTRACTS.AgentCommand.REVIEW,
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir) / "repo"
            repo_root.mkdir()
            payload = AGENT_REVIEW_CONTEXT.build_review_context_payload(
                repo_root=repo_root,
                task_config_path=AGENT_TASK_CONFIG_FILE,
                environment={"REVIEW_BASE_SHA": "a" * 40, "REVIEW_HEAD_SHA": "b" * 40},
            )
            payload["raw_pr_body"] = "unvalidated data"
            context_file = Path(temp_dir) / "review-context.json"
            context_file.write_text(json.dumps(payload), encoding="utf-8")
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(repo_root, 30)
            args = argparse.Namespace(
                context_file=str(context_file),
                task_settings=settings,
                resolved_model="gpt-test",
                output_file=str(Path(temp_dir) / "review.json"),
            )
            task = agent_tasks.ReviewAgentTask()
            with mock.patch.object(
                task,
                "run_agent",
                new=mock.AsyncMock(),
            ) as run_agent:
                with self.assertRaisesRegex(ValueError, "unexpected: raw_pr_body"):
                    asyncio.run(task.run(args))

        run_agent.assert_not_awaited()

    def test_hidden_runtime_files_are_restored_after_agent_failure(self):
        agent_tasks = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_TASKS_SCRIPT,
            "agent_runtime.tasks.configured_fake_sdk_hidden_files",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            context_file = Path(temp_dir) / "review-context.json"
            event_file = Path(temp_dir) / "event.json"
            context_file.write_bytes(b"original context")
            event_file.write_bytes(b"raw event payload")
            context_file.chmod(0o600)
            event_file.chmod(0o640)

            with self.assertRaisesRegex(RuntimeError, "agent failed"):
                with agent_tasks.hide_runtime_files(
                    {context_file: b"validated context", event_file: None}
                ):
                    self.assertFalse(context_file.exists())
                    self.assertFalse(event_file.exists())
                    raise RuntimeError("agent failed")

            self.assertEqual(context_file.read_bytes(), b"validated context")
            self.assertEqual(event_file.read_bytes(), b"raw event payload")
            self.assertEqual(context_file.stat().st_mode & 0o777, 0o600)
            self.assertEqual(event_file.stat().st_mode & 0o777, 0o640)

    def test_runner_parser_separates_review_context_from_edit_prompts(self):
        runner = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_RUNNER_SCRIPT,
            "agent_runtime.openai_agent_runner_fake_sdk_parser",
        )
        parser = runner.build_parser()
        subparsers = next(
            action
            for action in parser._actions
            if isinstance(action, argparse._SubParsersAction)
        )
        review_options = {
            option
            for action in subparsers.choices["run-review"]._actions
            for option in action.option_strings
        }
        repair_options = {
            option
            for action in subparsers.choices["run-repair"]._actions
            for option in action.option_strings
        }

        self.assertIn("--context-file", review_options)
        self.assertNotIn("--prompt-file", review_options)
        self.assertNotIn("--schema-file", review_options)
        self.assertNotIn("--max-prompt-chars", review_options)
        self.assertNotIn("--max-review-files", review_options)
        self.assertNotIn("--max-review-changed-lines", review_options)
        self.assertIn("--prompt-file", repair_options)
        self.assertIn("--max-prompt-chars", repair_options)
        self.assertNotIn("--context-file", repair_options)

    def test_openai_agent_runner_uses_type_specific_turn_defaults(self):
        self.assertEqual(
            OPENAI_AGENT_TASK_CONFIG.resolve_agent_task_settings(
                AGENT_TASK_CONFIG_FILE,
                OPENAI_AGENT_CONTRACTS.AgentCommand.REVIEW,
            ).max_turns,
            OPENAI_REVIEW_MAX_TURNS,
        )
        self.assertEqual(
            OPENAI_AGENT_TASK_CONFIG.resolve_agent_task_settings(
                AGENT_TASK_CONFIG_FILE,
                OPENAI_AGENT_CONTRACTS.AgentCommand.REPAIR,
            ).max_turns,
            OPENAI_PATCH_MAX_TURNS,
        )
        self.assertEqual(
            OPENAI_AGENT_TASK_CONFIG.resolve_agent_task_settings(
                AGENT_TASK_CONFIG_FILE,
                OPENAI_AGENT_CONTRACTS.AgentCommand.STABILIZATION,
            ).max_turns,
            OPENAI_PATCH_MAX_TURNS,
        )

        self.assertEqual(
            OPENAI_AGENT_TASK_CONFIG.resolve_agent_task_settings(
                AGENT_TASK_CONFIG_FILE,
                OPENAI_AGENT_CONTRACTS.AgentCommand.REVIEW,
                max_turns_override=12,
            ).max_turns,
            12,
        )

    def test_agent_runtime_dependencies_are_pinned(self):
        requirements = AGENT_REQUIREMENTS_FILE.read_text(encoding="utf-8").splitlines()

        self.assertEqual(
            set(requirements),
            {
                "openai-agents==0.17.7",
                "openai==2.44.0",
                "pydantic==2.13.4",
                "truststore==0.10.4",
            },
        )

    def test_agent_static_analysis_dependencies_are_pinned(self):
        pyproject = (REPO_ROOT / "tools/expkits-ci/pyproject.toml").read_text(encoding="utf-8")

        self.assertFalse((REPO_ROOT / ".github/agent-runtime/runtime/requirements-static-analysis.txt").exists())
        for requirement in (
            '"mypy==1.16.1"',
            '"pyflakes==3.3.2"',
            '"types-PyYAML==6.0.12.20250516"',
            '"vulture==2.14"',
        ):
            self.assertIn(requirement, pyproject)

    def test_agent_models_are_centralized_and_resolved_per_instance(self):
        model_config = json.loads(AGENT_MODEL_CONFIG_FILE.read_text(encoding="utf-8"))

        self.assertEqual(model_config["default_agent_model"], "gpt-5.5")
        self.assertEqual(set(model_config["agents"]), {"review", "repair", "stabilization"})
        for agent_name, agent_config in model_config["agents"].items():
            self.assertEqual(agent_config, {"model": "gpt-5.5"}, agent_name)

        self.assertEqual(
            OPENAI_AGENT_MODEL_CONFIG.resolve_agent_model(AGENT_MODEL_CONFIG_FILE, "review"),
            "gpt-5.5",
        )
        self.assertEqual(
            OPENAI_AGENT_MODEL_CONFIG.resolve_agent_model(AGENT_MODEL_CONFIG_FILE, "repair"),
            "gpt-5.5",
        )
        self.assertEqual(
            OPENAI_AGENT_MODEL_CONFIG.resolve_agent_model(AGENT_MODEL_CONFIG_FILE, "stabilization"),
            "gpt-5.5",
        )

    def test_agent_tasks_are_centrally_configured_per_command(self):
        task_config = json.loads(AGENT_TASK_CONFIG_FILE.read_text(encoding="utf-8"))

        self.assertEqual(set(task_config["tasks"]), {"run-review", "run-repair", "run-stabilization"})
        self.assertEqual(task_config["tasks"]["run-review"]["agent_instance"], "review")
        self.assertEqual(task_config["tasks"]["run-review"]["max_turns"], OPENAI_REVIEW_MAX_TURNS)
        self.assertNotIn("task_estimate_turns", task_config["tasks"]["run-review"])
        self.assertNotIn("max_prompt_chars", task_config["tasks"]["run-review"])
        self.assertEqual(task_config["tasks"]["run-review"]["max_review_files"], 120)
        self.assertEqual(task_config["tasks"]["run-review"]["max_review_changed_lines"], 15000)
        self.assertEqual(task_config["tasks"]["run-repair"]["agent_instance"], "repair")
        self.assertEqual(task_config["tasks"]["run-repair"]["max_turns"], OPENAI_PATCH_MAX_TURNS)
        self.assertEqual(task_config["tasks"]["run-stabilization"]["agent_instance"], "stabilization")
        self.assertEqual(task_config["tasks"]["run-stabilization"]["max_turns"], OPENAI_PATCH_MAX_TURNS)

        settings = OPENAI_AGENT_TASK_CONFIG.resolve_agent_task_settings(
            AGENT_TASK_CONFIG_FILE,
            "run-review",
            max_turns_override=12,
        )
        self.assertEqual(settings.agent_instance.value, "review")
        self.assertEqual(settings.max_turns, 12)
        self.assertIsNone(settings.max_prompt_chars)
        self.assertEqual(settings.max_review_files, 120)
        self.assertEqual(settings.max_review_changed_lines, 15000)

        with tempfile.TemporaryDirectory() as temp_dir:
            legacy_config = json.loads(AGENT_TASK_CONFIG_FILE.read_text(encoding="utf-8"))
            legacy_config["tasks"]["run-review"]["max_prompt_chars"] = 180000
            legacy_config_file = Path(temp_dir) / "legacy-agent-tasks.json"
            legacy_config_file.write_text(json.dumps(legacy_config), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "max_prompt_chars is no longer supported"):
                OPENAI_AGENT_TASK_CONFIG.load_agent_task_config(legacy_config_file)

        with self.assertRaisesRegex(ValueError, "max-prompt-chars is no longer supported"):
            OPENAI_AGENT_TASK_CONFIG.resolve_agent_task_settings(
                AGENT_TASK_CONFIG_FILE,
                "run-review",
                max_prompt_chars_override=180000,
            )

    def test_review_runtime_assets_do_not_own_scripts(self):
        self.assertFalse((AGENT_REVIEW_ROOT / "scripts").exists())
        self.assertTrue(AGENT_REVIEW_CONTEXT_SCRIPT.is_file())
        self.assertTrue(AGENT_REVIEW_INSTRUCTIONS_FILE.is_file())
        self.assertFalse((AGENT_REVIEW_ROOT / "prompts").exists())
        self.assertFalse((AGENT_REVIEW_ROOT / "schemas").exists())
        self.assertTrue(AGENT_REVIEW_FETCH_SCRIPT.is_file())
        self.assertTrue(AGENT_REVIEW_PUBLISH_SCRIPT.is_file())


if __name__ == "__main__":
    unittest.main()
