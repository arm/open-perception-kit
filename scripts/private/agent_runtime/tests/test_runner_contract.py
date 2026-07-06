################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import sys
from pathlib import Path
import unittest
import json
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tests'))
from agent_workflow_test_support import (  # noqa: E402
    AGENT_MODEL_CONFIG_FILE,
    AGENT_REQUIREMENTS_FILE,
    AGENT_REVIEW_FETCH_SCRIPT,
    AGENT_REVIEW_PROMPT_SCRIPT,
    AGENT_REVIEW_PUBLISH_SCRIPT,
    AGENT_REVIEW_ROOT,
    AGENT_TASK_CONFIG_FILE,
    OPENAI_AGENT_CONTRACTS,
    OPENAI_AGENT_CONTRACTS_SCRIPT,
    OPENAI_AGENT_MODEL_CONFIG,
    OPENAI_AGENT_MODEL_CONFIG_SCRIPT,
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
        self.assertIn("class AgentWorkflowTask", workflow_task_source)
        self.assertIn("class ConfiguredAgentWorkflowTask", task_source)
        self.assertIn("class ReviewAgentTask", task_source)
        self.assertEqual(task_source.count("class RepositoryEditAgentTask"), 1)
        self.assertIn("from .base import AgentWorkflowTask", task_source)
        self.assertIn("class ReviewResult", task_source)
        self.assertIn("return ReviewResult", task_source)
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
        self.assertIn("--agent-instance", runner_source)
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

    def test_openai_agent_runner_validates_review_schema_file_argument(self):
        agent_tasks = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_TASKS_SCRIPT,
            "agent_runtime.tasks.configured_fake_sdk_schema",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            schema_file = Path(temp_dir) / "review.schema.json"
            schema_file.write_text('{"type":"object"}\n', encoding="utf-8")

            agent_tasks.validate_schema_file(str(schema_file))
            with self.assertRaisesRegex(ValueError, "Review schema file does not exist"):
                agent_tasks.validate_schema_file(str(Path(temp_dir) / "missing.schema.json"))

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
        self.assertEqual(task_config["tasks"]["run-review"]["max_prompt_chars"], 180000)
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
            max_review_files_override=2,
        )
        self.assertEqual(settings.agent_instance.value, "review")
        self.assertEqual(settings.max_turns, 12)
        self.assertEqual(settings.max_review_files, 2)
        self.assertEqual(settings.max_review_changed_lines, 15000)

    def test_review_runtime_assets_do_not_own_scripts(self):
        self.assertFalse((AGENT_REVIEW_ROOT / "scripts").exists())
        self.assertTrue(AGENT_REVIEW_PROMPT_SCRIPT.is_file())
        self.assertTrue(AGENT_REVIEW_FETCH_SCRIPT.is_file())
        self.assertTrue(AGENT_REVIEW_PUBLISH_SCRIPT.is_file())


if __name__ == "__main__":
    unittest.main()
