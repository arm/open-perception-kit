################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
from email.message import Message
import io
import importlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import types
import urllib.error
import urllib.parse
import unittest
from unittest import mock
from typing import Any, cast
import zipfile

import yaml


REPO_ROOT = Path(__file__).resolve().parents[3]
WORKFLOW_FILE = REPO_ROOT / ".github/workflows/workflow-action-update-agent.yml"
REUSABLE_WORKFLOW_FILE = REPO_ROOT / ".github/workflows/workflow-action-update-agent-reusable.yml"
STABILIZER_WORKFLOW_FILE = REPO_ROOT / ".github/workflows/agent-stabilize-pr.yml"
WORKFLOW_AUDIT_FILE = REPO_ROOT / ".github/workflows/workflow-audit.yml"
AGENT_REVIEW_WORKFLOW_FILE = REPO_ROOT / ".github/workflows/agent-review.yml"
PEK_CI_WORKFLOW_FILE = REPO_ROOT / ".github/workflows/pek-ci.yml"
SONAR_WORKFLOW_FILE = REPO_ROOT / ".github/workflows/sonar.yml"
WORKFLOW_AUDIT_REPORT_SCRIPT = REPO_ROOT / "scripts/private/workflow_audit_report.py"
QUALITY_CHECKS_SCRIPT = REPO_ROOT / "tools/expkits-ci/expkits_ci/quality_checks.py"
AGENT_REVIEW_ROOT = REPO_ROOT / ".github/agent-runtime/review"
AGENT_REVIEW_FETCH_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/fetch.py"
AGENT_REVIEW_PUBLISH_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/publish.py"
AGENT_REVIEW_RUN_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/local_runner.py"
AGENT_REVIEW_PROMPT_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/prompt.py"
AGENT_REVIEW_COMMENTS_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/comments.py"
AGENT_REVIEW_DIFF_ANCHORS_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/diff_anchors.py"
AGENT_REVIEW_GITHUB_PUBLISH_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/github_publish.py"
AGENT_REVIEW_MARKDOWN_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/markdown.py"
AGENT_REVIEW_STATE_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/state.py"
AGENT_REVIEW_PROMPT_TEMPLATE = REPO_ROOT / ".github/agent-runtime/review/prompts/review.md.in"
AGENT_REQUIREMENTS_FILE = REPO_ROOT / ".github/agent-runtime/runtime/requirements-openai-agents.txt"
AGENT_STATIC_REQUIREMENTS_FILE = REPO_ROOT / ".github/agent-runtime/runtime/requirements-static-analysis.txt"
AGENT_MYPY_CONFIG_FILE = REPO_ROOT / ".github/agent-runtime/runtime/mypy.ini"
AGENT_MODEL_CONFIG_FILE = REPO_ROOT / ".github/agent-runtime/runtime/agent-models.json"
AGENT_TASK_CONFIG_FILE = REPO_ROOT / ".github/agent-runtime/runtime/agent-tasks.json"
OPENAI_AGENT_RUNNER_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/openai_agent_runner.py"
OPENAI_AGENT_INIT_FILE = REPO_ROOT / "scripts/private/agent_runtime/__init__.py"
OPENAI_AGENT_WORKFLOW_TASK_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/tasks/base.py"
OPENAI_AGENT_TASKS_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/tasks/configured.py"
OPENAI_AGENT_CONTRACTS_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/contracts.py"
OPENAI_AGENT_GITHUB_API_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/github_api.py"
OPENAI_AGENT_GITHUB_ACTIONS_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/github_actions.py"
OPENAI_AGENT_REPO_TOOLS_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/tools/repo.py"
OPENAI_AGENT_SHELL_TOOLS_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/tools/shell.py"
OPENAI_AGENT_PATH_TOOLS_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/tools/paths.py"
OPENAI_AGENT_RUNTIME_CONTEXT_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/runtime_context.py"
OPENAI_AGENT_SDK_RUNTIME_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/sdk_runtime.py"
OPENAI_AGENT_TASK_CONFIG_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/config/task.py"
OPENAI_AGENT_TASK_ESTIMATOR_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/tasks/estimator.py"
OPENAI_AGENT_MODEL_CONFIG_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/config/model.py"
OPENAI_AGENT_REVIEW_OUTPUT_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/output_filter.py"
OPENAI_AGENT_STATIC_ANALYSIS_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/static_analysis.py"
OPENAI_AGENT_WORKFLOW_PY_FILES = sorted((REPO_ROOT / "scripts/private/agent_runtime").rglob("*.py"))
OPENAI_AGENT_WORKFLOW_POLICY_FILES = [
    path
    for path in OPENAI_AGENT_WORKFLOW_PY_FILES
    if path != OPENAI_AGENT_CONTRACTS_SCRIPT
]
HELPER_SCRIPT = REPO_ROOT / "scripts/private/workflow_action_update_agent/cli.py"
HELPER_RUNTIME_SCRIPT = REPO_ROOT / "scripts/private/workflow_action_update_agent/runtime.py"
HELPER_REPAIR_SCRIPT = REPO_ROOT / "scripts/private/workflow_action_update_agent/repair.py"
HELPER_GITHUB_WORKFLOWS_SCRIPT = REPO_ROOT / "scripts/private/workflow_action_update_agent/github_workflows.py"
HELPER_STABILIZATION_SCRIPT = REPO_ROOT / "scripts/private/workflow_action_update_agent/stabilization.py"
HELPER_ACTION_FILE = REPO_ROOT / ".github/actions/workflow-action-update-agent-helper/action.yml"
WORKFLOW_AUTOMATION_ROOT = REPO_ROOT / ".github/agent-runtime/workflow-action-update-agent"
PROMPT_TEMPLATE_ROOT = WORKFLOW_AUTOMATION_ROOT / "prompts"
PROFILE_ROOT = WORKFLOW_AUTOMATION_ROOT / "profiles"
GOAL_TEMPLATE = PROMPT_TEMPLATE_ROOT / "repair-goal.md.in"
STABILIZE_GOAL_TEMPLATE = PROMPT_TEMPLATE_ROOT / "stabilize-goal.md.in"
CONTEXT_TEMPLATE = PROMPT_TEMPLATE_ROOT / "context.md"
CONSTRAINTS_TEMPLATE = PROMPT_TEMPLATE_ROOT / "constraints.md"
VALIDATION_TEMPLATE = PROMPT_TEMPLATE_ROOT / "validation.md.in"
PROFILE_FILE = PROFILE_ROOT / "profile.json"
WORKFLOW_AUDIT_PROFILE_FILE = PROFILE_ROOT / "workflow-audit-profile.json"
PULL_REQUEST_TEMPLATE = REPO_ROOT / ".github/PULL_REQUEST_TEMPLATE.md"
WORKFLOW_AUTOMATION_AGENTS_FILE = WORKFLOW_AUTOMATION_ROOT / "AGENTS.md"
HELPER_AGENTS_FILE = HELPER_SCRIPT.parent / "AGENTS.md"
REPAIR_BRANCH = "feature/EXPKITS-4242/bot-workflow-action-update-agent-run-12345"  # pragma: allowlist secret
OPENAI_AGENT_RUNNER_LABEL = "self-hosted-ubuntu-latest"
OPENAI_REVIEW_MAX_TURNS = 60
OPENAI_PATCH_MAX_TURNS = 30


def load_yaml(path: Path) -> Any:
    return yaml.load(path.read_text(encoding="utf-8"), Loader=yaml.BaseLoader)


def load_python_module(path: Path, module_name: str):
    spec = importlib.util.spec_from_file_location(module_name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load module {module_name} from {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    return module


def load_quality_checks_module():
    sentinel = object()
    previous_modules: dict[str, object] = {}

    def install_module(name: str, module: types.ModuleType) -> None:
        previous_modules[name] = sys.modules.get(name, sentinel)
        sys.modules[name] = module

    package = types.ModuleType("expkits_ci")
    package.__path__ = []  # type: ignore[attr-defined]
    license_module = types.ModuleType("expkits_ci.license_template_manager")
    file_utils_module = types.ModuleType("expkits_ci.file_utils")
    git_module = types.ModuleType("git")
    requests_module = types.ModuleType("requests")

    class LicenseTemplateManager:
        pass

    class FileUtils:
        @staticmethod
        def get_project_root() -> str:
            return str(REPO_ROOT)

    class GitCommandError(Exception):
        pass

    class Repo:
        pass

    license_module.LicenseTemplateManager = LicenseTemplateManager  # type: ignore[attr-defined]
    file_utils_module.FileUtils = FileUtils  # type: ignore[attr-defined]
    git_module.Repo = Repo  # type: ignore[attr-defined]
    git_module.GitCommandError = GitCommandError  # type: ignore[attr-defined]

    for name, module in (
        ("expkits_ci", package),
        ("expkits_ci.license_template_manager", license_module),
        ("expkits_ci.file_utils", file_utils_module),
        ("git", git_module),
        ("requests", requests_module),
    ):
        install_module(name, module)

    try:
        return load_python_module(QUALITY_CHECKS_SCRIPT, "quality_checks_under_test")
    finally:
        for name, previous in previous_modules.items():
            if previous is sentinel:
                sys.modules.pop(name, None)
            else:
                sys.modules[name] = previous  # type: ignore[assignment]


def load_agent_workflow_module(path: Path, module_name: str):
    module_path = str(OPENAI_AGENT_RUNNER_SCRIPT.parent.parent)
    sys.path.insert(0, module_path)
    try:
        return load_python_module(path, module_name)
    finally:
        sys.path.remove(module_path)


def load_workflow_helper_module(module_name: str):
    module_path = str(HELPER_SCRIPT.parent.parent)
    sys.path.insert(0, module_path)
    try:
        return importlib.import_module(module_name)
    finally:
        sys.path.remove(module_path)


def step_map(job: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {
        step["name"]: step
        for step in job.get("steps", [])
        if isinstance(step, dict) and "name" in step
    }


def http_headers(values: dict[str, str] | None = None) -> Message[str, str]:
    headers: Message[str, str] = Message()
    for key, value in (values or {}).items():
        headers[key] = value
    return headers


def build_zip_archive(files: dict[str, str]) -> bytes:
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as archive:
        for path, content in files.items():
            archive.writestr(path, content)
    return buffer.getvalue()


def load_agent_workflow_module_with_fake_sdk(path: Path, module_name: str):
    fake_agents = types.SimpleNamespace(
        Agent=object,
        RunConfig=object,
        Runner=object,
        function_tool=lambda function: function,
    )
    fake_truststore = types.SimpleNamespace(inject_into_ssl=lambda: None)
    fake_pydantic = types.SimpleNamespace(
        BaseModel=object,
        ConfigDict=lambda **_kwargs: {},
        Field=lambda *args, **_kwargs: args[0] if args else None,
    )
    module_path = str(OPENAI_AGENT_RUNNER_SCRIPT.parent.parent)
    with mock.patch.dict(
        sys.modules,
        {"agents": fake_agents, "truststore": fake_truststore, "pydantic": fake_pydantic},
    ):
        sys.path.insert(0, module_path)
        try:
            return load_python_module(path, module_name)
        finally:
            sys.path.remove(module_path)


WORKFLOW_AUDIT_REPORT = load_python_module(WORKFLOW_AUDIT_REPORT_SCRIPT, "workflow_audit_report")
OPENAI_AGENT_CONTRACTS = load_agent_workflow_module(
    OPENAI_AGENT_CONTRACTS_SCRIPT,
    "agent_runtime.contracts",
)
OPENAI_AGENT_GITHUB_API = load_agent_workflow_module(
    OPENAI_AGENT_GITHUB_API_SCRIPT,
    "agent_runtime.github_api",
)
OPENAI_AGENT_GITHUB_ACTIONS = load_agent_workflow_module(
    OPENAI_AGENT_GITHUB_ACTIONS_SCRIPT,
    "agent_runtime.github_actions",
)
AGENT_REVIEW_STATE = load_agent_workflow_module(
    AGENT_REVIEW_STATE_SCRIPT,
    "agent_runtime.review.state",
)
AGENT_REVIEW_FETCH = load_agent_workflow_module(
    AGENT_REVIEW_FETCH_SCRIPT,
    "agent_runtime.review.fetch",
)
AGENT_REVIEW_PUBLISH = load_agent_workflow_module(
    AGENT_REVIEW_PUBLISH_SCRIPT,
    "agent_runtime.review.publish",
)
AGENT_REVIEW_COMMENTS = load_agent_workflow_module(
    AGENT_REVIEW_COMMENTS_SCRIPT,
    "agent_runtime.review.comments",
)
AGENT_REVIEW_DIFF_ANCHORS = load_agent_workflow_module(
    AGENT_REVIEW_DIFF_ANCHORS_SCRIPT,
    "agent_runtime.review.diff_anchors",
)
AGENT_REVIEW_GITHUB_PUBLISH = load_agent_workflow_module(
    AGENT_REVIEW_GITHUB_PUBLISH_SCRIPT,
    "agent_runtime.review.github_publish",
)
AGENT_REVIEW_MARKDOWN = load_agent_workflow_module(
    AGENT_REVIEW_MARKDOWN_SCRIPT,
    "agent_runtime.review.markdown",
)
AGENT_REVIEW_PROMPT = load_agent_workflow_module(
    AGENT_REVIEW_PROMPT_SCRIPT,
    "agent_runtime.review.prompt",
)
OPENAI_AGENT_MODEL_CONFIG = load_agent_workflow_module(
    OPENAI_AGENT_MODEL_CONFIG_SCRIPT,
    "agent_runtime.config.model",
)
OPENAI_AGENT_TASK_CONFIG = load_agent_workflow_module(
    OPENAI_AGENT_TASK_CONFIG_SCRIPT,
    "agent_runtime.config.task",
)
AGENT_REVIEW_OUTPUT = load_agent_workflow_module(
    OPENAI_AGENT_REVIEW_OUTPUT_SCRIPT,
    "agent_runtime.review.output_filter",
)
OPENAI_AGENT_STATIC_ANALYSIS = load_agent_workflow_module(
    OPENAI_AGENT_STATIC_ANALYSIS_SCRIPT,
    "agent_runtime.static_analysis",
)
OPENAI_AGENT_RUNTIME_CONTEXT = load_agent_workflow_module(
    OPENAI_AGENT_RUNTIME_CONTEXT_SCRIPT,
    "agent_runtime.runtime_context",
)
HELPER_RUNTIME = load_workflow_helper_module("workflow_action_update_agent.runtime")
HELPER_REPAIR = load_workflow_helper_module("workflow_action_update_agent.repair")
HELPER_GITHUB_WORKFLOWS = load_workflow_helper_module("workflow_action_update_agent.github_workflows")
HELPER_STABILIZATION = load_workflow_helper_module("workflow_action_update_agent.stabilization")


class WorkflowActionUpdateAgentStaticTests(unittest.TestCase):
    def test_manual_wrapper_calls_reusable_workflow_with_minimal_inputs(self):
        workflow = load_yaml(WORKFLOW_FILE)
        dispatch_inputs = workflow["on"]["workflow_dispatch"]["inputs"]
        repair_job = workflow["jobs"]["run-workflow-action-update-agent"]
        stabilize_job = workflow["jobs"]["run-agent-stabilizer"]

        self.assertEqual(
            set(dispatch_inputs.keys()),
            {"source_run_id", "pr_number", "head_sha", "target_branch", "ticket_id", "profile_path"},
        )
        self.assertNotIn("workflow_run", workflow["on"])
        self.assertEqual(repair_job["uses"], "./.github/workflows/workflow-action-update-agent-reusable.yml")
        self.assertEqual(stabilize_job["uses"], "./.github/workflows/agent-stabilize-pr.yml")
        self.assertEqual(repair_job["if"], "${{ inputs.pr_number == '' }}")
        self.assertEqual(stabilize_job["if"], "${{ inputs.pr_number != '' }}")
        self.assertEqual(
            set(repair_job["with"].keys()),
            {"source_run_id", "target_branch", "ticket_id", "profile_path"},
        )
        self.assertEqual(
            set(stabilize_job["with"].keys()),
            {"pr_number", "head_sha", "source_run_id", "ticket_id", "profile_path"},
        )
        self.assertNotIn("source_workflow_conclusion", repair_job["with"])
        self.assertNotIn("source_head_branch", repair_job["with"])
        self.assertNotIn("source_head_repository", repair_job["with"])
        self.assertEqual(repair_job["permissions"]["actions"], "write")
        self.assertEqual(stabilize_job["permissions"]["actions"], "read")
        self.assertEqual(repair_job["secrets"], "inherit")
        self.assertEqual(stabilize_job["secrets"], "inherit")

    def test_reusable_workflow_uses_profile_and_composite_action(self):
        workflow = load_yaml(REUSABLE_WORKFLOW_FILE)
        inputs = workflow["on"]["workflow_call"]["inputs"]
        agent_job = workflow["jobs"]["agent-fix"]
        stabilize_job = workflow["jobs"]["stabilize-pr"]
        agent_steps = step_map(agent_job)
        stabilize_steps = step_map(stabilize_job)
        agent_step_names = [
            step.get("name") or step.get("id") or step.get("uses")
            for step in agent_job["steps"]
        ]

        self.assertEqual(
            set(inputs.keys()),
            {"source_run_id", "target_branch", "ticket_id", "profile_path"},
        )
        self.assertEqual(
            inputs["profile_path"]["default"],
            ".github/agent-runtime/workflow-action-update-agent/profiles/profile.json",
        )

        helper_steps = []
        for job in workflow["jobs"].values():
            for step in job["steps"]:
                if step.get("uses") == "./.github/actions/workflow-action-update-agent-helper":
                    helper_steps.append(step["with"]["command"])

        self.assertEqual(
            set(helper_steps),
            {
                "resolve-inputs",
                "collect-context",
                "build-markdown",
                "package-repository-changes",
                "require-generated-changes",
                "apply-repair-changes-and-push",
                "create-draft-pr",
                "stabilize-pr",
            },
        )
        apply_step = next(
            step
            for step in workflow["jobs"]["open-pr"]["steps"]
            if step.get("with", {}).get("command") == "apply-repair-changes-and-push"
        )
        self.assertEqual(
            apply_step["with"]["target-branch"],
            "${{ needs.prepare.outputs.target_branch }}",
        )
        self.assertEqual(agent_job["runs-on"], OPENAI_AGENT_RUNNER_LABEL)
        self.assertEqual(stabilize_job["runs-on"], "ubuntu-latest")
        self.assertNotIn("Download source artifact context", agent_steps)
        self.assertIn("Set up Agent Python", agent_steps)
        self.assertIn("Install OpenAI agent runtime", agent_steps)
        self.assertIn("Run OpenAI SDK repair agent", agent_steps)
        self.assertEqual(agent_job["steps"][0]["with"]["persist-credentials"], "false")
        self.assertLess(
            agent_step_names.index("Set up Agent Python"),
            agent_step_names.index("Install OpenAI agent runtime"),
        )
        self.assertLess(
            agent_step_names.index("Run static regression tests"),
            agent_step_names.index("Run OpenAI SDK repair agent"),
        )
        self.assertLess(
            agent_step_names.index("Run OpenAI SDK repair agent"),
            agent_step_names.index("patch"),
        )
        self.assertNotIn("Prime OpenAI SDK CLI", stabilize_steps)
        self.assertNotIn("Apply deterministic workflow freshness patch", agent_steps)

        python_step = agent_steps["Set up Agent Python"]
        self.assertEqual(python_step["uses"], "actions/setup-python@v6")
        self.assertEqual(python_step["with"]["python-version"], "3.10")
        install_step = agent_steps["Install OpenAI agent runtime"]
        self.assertEqual(install_step["shell"], "bash")
        self.assertIn("python3 -m venv .agent-runtime/openai-agent-venv", install_step["run"])
        self.assertIn(
            ".agent-runtime/openai-agent-venv/bin/python -m pip install -r .github/agent-runtime/runtime/requirements-openai-agents.txt",
            install_step["run"],
        )
        agent_step = agent_steps["Run OpenAI SDK repair agent"]
        self.assertEqual(agent_step["shell"], "bash")
        self.assertEqual(
            agent_step["env"]["OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS"],
            "${{ secrets.OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS }}",
        )
        self.assertIn(
            ".agent-runtime/openai-agent-venv/bin/python scripts/private/agent_runtime/openai_agent_runner.py run-repair",
            agent_step["run"],
        )
        self.assertIn("--prompt-file .agent-runtime/workflow-action-update-agent/goal.md", agent_step["run"])
        self.assertEqual(
            agent_step["run"].count("${{ runner.temp }}/workflow-action-update-agent-agent-output.md"),
            1,
        )
        self.assertNotIn("--agent-instance", agent_step["run"])
        self.assertNotIn("--max-turns", agent_step["run"])
        self.assertNotIn("--model", agent_step["run"])
        static_regression_step = agent_steps["Run static regression tests"]
        self.assertIn(
            ".agent-runtime/openai-agent-venv/bin/python -m pip install -r .github/agent-runtime/runtime/requirements-static-analysis.txt",
            static_regression_step["run"],
        )
        self.assertIn(
            ".agent-runtime/openai-agent-venv/bin/python scripts/private/agent_runtime/static_analysis.py",
            static_regression_step["run"],
        )
        self.assertEqual(stabilize_job["permissions"]["actions"], "write")
        self.assertEqual(stabilize_steps["Checkout workflow helpers"]["uses"], "actions/checkout@v6")
        self.assertEqual(
            stabilize_steps["Stabilize repair PR"]["uses"],
            "./.github/actions/workflow-action-update-agent-helper",
        )
        self.assertEqual(
            stabilize_steps["Stabilize repair PR"]["env"]["GITHUB_TOKEN"],
            "${{ github.token }}",
        )
        self.assertEqual(
            stabilize_steps["Stabilize repair PR"]["env"]["GH_TOKEN"],
            "${{ secrets.EXPKITS_AGENT_TOKEN || github.token }}",
        )
        self.assertEqual(
            stabilize_steps["Stabilize repair PR"]["with"]["merge-when-stable"],
            "true",
        )

    def test_helper_action_exposes_structured_outputs(self):
        action = load_yaml(HELPER_ACTION_FILE)

        self.assertEqual(action["runs"]["using"], "composite")
        self.assertEqual(
            action["inputs"]["profile-path"]["default"],
            ".github/agent-runtime/workflow-action-update-agent/profiles/profile.json",
        )
        self.assertIn("command", action["inputs"])
        self.assertEqual(action["inputs"]["merge-when-stable"]["default"], "false")
        self.assertIn("should_run", action["outputs"])
        self.assertIn("source_pr_number", action["outputs"])
        self.assertIn("repair_branch", action["outputs"])
        self.assertIn("agent_model", action["outputs"])
        self.assertIn("has_changes", action["outputs"])
        self.assertIn("head_sha", action["outputs"])
        self.assertIn("pr_number", action["outputs"])
        self.assertIn("review_recommendation", action["outputs"])
        self.assertIn("review_run_id", action["outputs"])
        self.assertIn("${{ github.action_path }}", action["runs"]["steps"][0]["run"])

    def test_helper_script_uses_github_workspace_as_repo_root(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            with mock.patch.dict(os.environ, {"GITHUB_WORKSPACE": temp_dir}, clear=False):
                workspace_helper = load_python_module(
                    HELPER_RUNTIME_SCRIPT,
                    "workflow_action_update_agent_workspace_root",
                )

        self.assertEqual(workspace_helper.REPO_ROOT, Path(temp_dir).resolve())
        self.assertEqual(
            workspace_helper.resolve_repo_path(
                ".github/agent-runtime/workflow-action-update-agent/profiles/profile.json"),
            Path(temp_dir).resolve() / ".github/agent-runtime/workflow-action-update-agent/profiles/profile.json",
        )
        self.assertEqual(
            workspace_helper.profile_config_root(
                ".workflow-action-update-agent-helper/.github/agent-runtime/workflow-action-update-agent/profiles/profile.json",
            ),
            Path(temp_dir).resolve() / ".workflow-action-update-agent-helper",
        )

    def test_download_github_archive_follows_redirect_location(self):
        redirect_error = urllib.error.HTTPError(
            url="https://api.github.com/repos/Arm-Debug/amp-dev-forge/actions/artifacts/1/zip",
            code=302,
            msg="Found",
            hdrs=http_headers({"Location": "https://objects.githubusercontent.com/archive.zip"}),
            fp=None,
        )
        opener = mock.Mock()
        opener.open.side_effect = redirect_error
        redirect_response = mock.MagicMock(status=200, reason="OK")
        redirect_response.read.return_value = b"zip-bytes"
        redirect_connection = mock.MagicMock()
        redirect_connection.getresponse.return_value = redirect_response

        with mock.patch.dict(os.environ, {"GH_TOKEN": "test-token"}, clear=False):
            with mock.patch("urllib.request.build_opener", return_value=opener):
                with mock.patch("http.client.HTTPSConnection", return_value=redirect_connection) as connection:
                    result = OPENAI_AGENT_GITHUB_API.download_github_archive(
                        "https://api.github.com/repos/Arm-Debug/amp-dev-forge/actions/artifacts/1/zip",
                    )

        self.assertEqual(result, b"zip-bytes")
        connection.assert_called_once_with("objects.githubusercontent.com", timeout=60)
        redirect_connection.request.assert_called_once_with(
            "GET",
            "/archive.zip",
            headers={"User-Agent": OPENAI_AGENT_CONTRACTS.GITHUB_USER_AGENT},
        )
        redirect_connection.close.assert_called_once_with()

    def test_download_github_archive_rejects_unsafe_redirect_location(self):
        cleartext_location = urllib.parse.urlunsplit(
            ("http", "objects.githubusercontent.com", "/archive.zip", "", "")
        )
        for location in (
            cleartext_location,
            "https://example.com/archive.zip",
        ):
            with self.subTest(location=location):
                redirect_error = urllib.error.HTTPError(
                    url="https://api.github.com/repos/Arm-Debug/amp-dev-forge/actions/artifacts/1/zip",
                    code=302,
                    msg="Found",
                    hdrs=http_headers({"Location": location}),
                    fp=None,
                )
                opener = mock.Mock()
                opener.open.side_effect = redirect_error

                with mock.patch.dict(os.environ, {"GH_TOKEN": "test-token"}, clear=False):
                    with mock.patch("urllib.request.build_opener", return_value=opener):
                        with self.assertRaisesRegex(ValueError, "GitHub archive redirect URL"):
                            OPENAI_AGENT_GITHUB_API.download_github_archive(
                                "https://api.github.com/repos/Arm-Debug/amp-dev-forge/actions/artifacts/1/zip",
                            )

    def test_github_api_json_requires_relative_endpoint(self):
        with self.assertRaisesRegex(ValueError, "must be relative"):
            OPENAI_AGENT_GITHUB_API.github_api_json("https://api.github.com/user")

    def test_github_api_headers_distinguish_required_and_optional_auth(self):
        with mock.patch.dict(os.environ, {"GH_TOKEN": "env-token"}, clear=True):
            required_headers = OPENAI_AGENT_GITHUB_API.github_api_headers(token="")
            optional_headers = OPENAI_AGENT_GITHUB_API.github_api_headers(
                token=None,
                require_token=False,
            )

        self.assertEqual(required_headers["Authorization"], "Bearer env-token")
        self.assertNotIn("Authorization", optional_headers)

    def test_github_api_query_endpoint_encodes_parameters(self):
        endpoint = OPENAI_AGENT_GITHUB_API.github_api_query_endpoint(
            "repos/Arm-Debug/amp-dev-forge/actions/workflows/agent-review.yml/runs",
            {"branch": "feature/with space&marker", "per_page": 20},
        )

        self.assertEqual(
            endpoint,
            "repos/Arm-Debug/amp-dev-forge/actions/workflows/agent-review.yml/runs"
            "?branch=feature%2Fwith+space%26marker&per_page=20",
        )

    def test_extract_archive_bytes_rejects_members_outside_destination(self):
        for member_template in ("../outside.txt", "{temp_root}/outside.txt"):
            with self.subTest(member_template=member_template):
                with tempfile.TemporaryDirectory() as temp_dir:
                    temp_root = Path(temp_dir)
                    destination = temp_root / "destination"
                    member_name = member_template.format(temp_root=temp_root)
                    archive = build_zip_archive(
                        {
                            "logs/job.txt": "hello from logs\n",
                            member_name: "owned\n",
                        }
                    )

                    with self.assertRaisesRegex(RuntimeError, "escapes destination"):
                        OPENAI_AGENT_GITHUB_API.extract_archive_bytes(archive, destination)

                    self.assertFalse((temp_root / "outside.txt").exists())
                    self.assertFalse((destination / "logs/job.txt").exists())

    def test_extract_archive_bytes_returns_extracted_files(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            destination = Path(temp_dir) / "destination"
            archive = build_zip_archive(
                {
                    "logs/job.txt": "hello from logs\n",
                    "artifact/report.md": "# report\n",
                }
            )

            extracted = OPENAI_AGENT_GITHUB_API.extract_archive_bytes(archive, destination)

        self.assertEqual(
            [path.relative_to(destination).as_posix() for path in extracted],
            ["artifact/report.md", "logs/job.txt"],
        )

    def test_workflow_helper_does_not_use_unsafe_zip_extractall(self):
        content = OPENAI_AGENT_GITHUB_API_SCRIPT.read_text(encoding="utf-8")

        self.assertNotIn(".extractall(", content)

    def test_read_review_artifact_state_uses_safe_archive_extraction(self):
        archive = build_zip_archive(
            {
                "nested/review.json": json.dumps(
                    {
                        "overall_recommendation": "approve",
                        "summary": "Looks good.",
                    }
                )
            }
        )

        with mock.patch.object(
            AGENT_REVIEW_STATE,
            "github_api_json",
            return_value={
                "artifacts": [
                    {
                        "name": "agent-review-out",
                        "archive_download_url": "https://api.github.com/artifacts/1/zip",
                        "expired": False,
                    }
                ]
            },
        ):
            with mock.patch.object(AGENT_REVIEW_STATE, "download_github_archive", return_value=archive):
                review_state = AGENT_REVIEW_STATE.read_review_artifact_state(
                    repository="Arm-Debug/amp-dev-forge",
                    run_id="28000000001",
                    head_sha="deadbeef",
                )

        self.assertEqual(review_state["overall_recommendation"], "approve")
        self.assertEqual(review_state["run_id"], "28000000001")
        self.assertEqual(review_state["head_sha"], "deadbeef")

    def test_collect_context_uses_github_api_archives_on_self_hosted(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            context_root = Path(temp_dir) / "context"
            args = argparse.Namespace(
                context_root=str(context_root),
                source_run_id="12345",
                source_run_url="https://github.com/Arm-Debug/amp-dev-forge/actions/runs/12345",
                source_workflow_name="Workflow dependency freshness",
            )
            log_archive = build_zip_archive({"logs/job.txt": "hello from logs\n"})
            artifact_archive = build_zip_archive({"report.md": "# report\n"})

            with mock.patch.dict(
                os.environ,
                {"GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge", "GH_TOKEN": "test-token"},
                clear=False,
            ):
                with mock.patch.object(
                    OPENAI_AGENT_GITHUB_ACTIONS,
                    "github_api_json",
                    side_effect=[
                        {"id": 12345, "name": "Workflow dependency freshness"},
                        {
                            "artifacts": [
                                {
                                    "name": "workflow-dependency-freshness",
                                    "archive_download_url": "https://api.github.com/artifacts/1/zip",
                                    "id": 1,
                                    "expired": False,
                                }
                            ]
                        },
                    ],
                ):
                    with mock.patch.object(
                        OPENAI_AGENT_GITHUB_ACTIONS,
                        "download_github_archive",
                        side_effect=[log_archive, artifact_archive],
                    ):
                        result = HELPER_REPAIR.command_collect_context(args)
            self.assertEqual(result, 0)
            self.assertTrue((context_root / "source-run.json").is_file())
            self.assertIn("hello from logs", (context_root / "source-run.log").read_text(encoding="utf-8"))
            self.assertTrue(
                (context_root / "artifacts/workflow-dependency-freshness/report.md").is_file()
            )

    def test_agent_review_workflow_uses_openai_sdk_proxy_flow(self):
        workflow = load_yaml(AGENT_REVIEW_WORKFLOW_FILE)
        pull_request_trigger = workflow["on"]["pull_request"]
        dispatch_inputs = workflow["on"]["workflow_dispatch"]["inputs"]
        review_job = workflow["jobs"]["review"]
        auto_stabilize_job = workflow["jobs"]["auto-stabilize-pr"]
        review_steps = step_map(review_job)

        self.assertIn("labeled", pull_request_trigger["types"])
        self.assertEqual(workflow["permissions"]["actions"], "read")
        self.assertEqual(workflow["permissions"]["contents"], "read")
        self.assertEqual(workflow["permissions"]["pull-requests"], "write")
        self.assertEqual(review_job["runs-on"], OPENAI_AGENT_RUNNER_LABEL)
        self.assertIn("github.event.action != 'labeled'", review_job["if"])
        self.assertIn("github.event.label.name == 'agent-autorepair'", review_job["if"])
        self.assertEqual(auto_stabilize_job["needs"], "review")
        self.assertEqual(
            auto_stabilize_job["uses"],
            "./.github/workflows/agent-stabilize-pr.yml",
        )
        self.assertIn("github.event_name == 'pull_request'", auto_stabilize_job["if"])
        self.assertIn("needs.review.result == 'success'", auto_stabilize_job["if"])
        self.assertIn("github.event.pull_request.head.repo.full_name == github.repository", auto_stabilize_job["if"])
        self.assertIn(
            "contains(github.event.pull_request.labels.*.name, 'agent-autorepair')",
            auto_stabilize_job["if"],
        )
        self.assertIn("github.event.action != 'labeled'", auto_stabilize_job["if"])
        self.assertIn("github.event.label.name == 'agent-autorepair'", auto_stabilize_job["if"])
        self.assertEqual(auto_stabilize_job["permissions"]["actions"], "read")
        self.assertEqual(auto_stabilize_job["permissions"]["contents"], "write")
        self.assertEqual(auto_stabilize_job["permissions"]["pull-requests"], "write")
        self.assertEqual(auto_stabilize_job["with"]["pr_number"], "${{ github.event.pull_request.number }}")
        self.assertEqual(auto_stabilize_job["with"]["head_sha"], "${{ github.event.pull_request.head.sha }}")
        self.assertEqual(auto_stabilize_job["with"]["source_run_id"], "${{ github.run_id }}")
        self.assertEqual(auto_stabilize_job["with"]["profile_path"],
                         ".github/agent-runtime/workflow-action-update-agent/profiles/profile.json")
        self.assertEqual(auto_stabilize_job["secrets"], "inherit")
        self.assertIn("base_ref", dispatch_inputs)
        self.assertIn("head_ref", dispatch_inputs)
        self.assertEqual(dispatch_inputs["head_ref"]["default"], "")
        self.assertIn("defaults to the workflow run SHA", dispatch_inputs["head_ref"]["description"])
        self.assertEqual(
            list(review_steps),
            [
                "Checkout pull request head",
                "Fetch Agent review base ref",
                "Set up Agent Python",
                "Render Agent review prompt",
                "Install OpenAI agent runtime",
                "Run Agent workflow static analysis",
                "Run OpenAI SDK review",
                "Render review summary",
                "Publish review summary comment",
                "Upload review artifacts",
            ],
        )
        selected_head_ref = (
            "${{ github.event_name == 'workflow_dispatch' && "
            "(github.event.inputs.head_ref || github.sha) || github.event.pull_request.head.sha || github.sha }}"
        )
        python_step = review_steps["Set up Agent Python"]
        self.assertEqual(python_step["uses"], "actions/setup-python@v6")
        self.assertEqual(python_step["with"]["python-version"], "3.10")
        install_step = review_steps["Install OpenAI agent runtime"]
        self.assertEqual(install_step["shell"], "bash")
        self.assertIn("python3 -m venv .agent-runtime/openai-agent-venv", install_step["run"])
        self.assertIn(
            ".agent-runtime/openai-agent-venv/bin/python -m pip install -r .github/agent-runtime/runtime/requirements-openai-agents.txt",
            install_step["run"],
        )
        static_step = review_steps["Run Agent workflow static analysis"]
        self.assertEqual(static_step["shell"], "bash")
        self.assertIn(
            ".agent-runtime/openai-agent-venv/bin/python -m pip install -r .github/agent-runtime/runtime/requirements-static-analysis.txt",
            static_step["run"],
        )
        self.assertIn(
            ".agent-runtime/openai-agent-venv/bin/python scripts/private/agent_runtime/static_analysis.py --base-ref \"${REVIEW_BASE_REF}\"",
            static_step["run"],
        )
        checkout_step = review_steps["Checkout pull request head"]
        self.assertEqual(checkout_step["with"]["ref"], selected_head_ref)
        fetch_step = review_steps["Fetch Agent review base ref"]
        self.assertEqual(fetch_step["shell"], "bash")
        self.assertEqual(fetch_step["env"]["GITHUB_TOKEN"], "${{ github.token }}")
        self.assertIn("REVIEW_BASE_REF", fetch_step["env"])
        self.assertIn('if [[ "${REVIEW_BASE_REF}" == origin/* ]]; then', fetch_step["run"])
        self.assertIn(
            'auth_header="$(printf \'x-access-token:%s\' "${GITHUB_TOKEN}" | base64 -w 0)"',
            fetch_step["run"],
        )
        self.assertIn(
            'git -c "http.https://github.com/.extraheader=AUTHORIZATION: basic ${auth_header}"',
            fetch_step["run"],
        )
        self.assertIn(
            'fetch --no-tags origin "+refs/heads/${base_branch}:refs/remotes/origin/${base_branch}"',
            fetch_step["run"],
        )
        render_step = review_steps["Render Agent review prompt"]
        self.assertEqual(render_step["env"]["REVIEW_HEAD_REF"], selected_head_ref)
        self.assertIn(
            "python3 scripts/private/agent_runtime/review/prompt.py",
            render_step["run"],
        )
        self.assertIn(
            "--output .github/agent-runtime/review/out/review.prompt.md",
            render_step["run"],
        )
        agent_step = review_steps["Run OpenAI SDK review"]
        self.assertEqual(agent_step["shell"], "bash")
        self.assertEqual(
            agent_step["env"]["OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS"],
            "${{ secrets.OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS }}",
        )
        self.assertIn(
            ".agent-runtime/openai-agent-venv/bin/python scripts/private/agent_runtime/openai_agent_runner.py run-review",
            agent_step["run"],
        )
        self.assertEqual(
            agent_step["run"].count("--schema-file .github/agent-runtime/review/schemas/review.schema.json"),
            1,
        )
        self.assertEqual(
            agent_step["run"].count("--output-file .github/agent-runtime/review/out/review.json"),
            1,
        )
        self.assertEqual(
            agent_step["run"].count("--prompt-file .github/agent-runtime/review/out/review.prompt.md"),
            1,
        )
        self.assertNotIn("--agent-instance", agent_step["run"])
        self.assertNotIn("--max-turns", agent_step["run"])
        self.assertNotIn("--model", agent_step["run"])
        publish_step = review_steps["Publish review summary comment"]
        self.assertEqual(
            publish_step["env"]["REVIEW_BASE_REF"],
            "${{ format('origin/{0}', github.base_ref) }}",
        )
        self.assertIn(
            ".agent-runtime/openai-agent-venv/bin/python scripts/private/agent_runtime/review/publish.py",
            publish_step["run"],
        )

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
        self.assertIn("class TaskEstimate", estimator_source)
        self.assertIn("class TaskEstimatorWorkflowTask", estimator_source)
        self.assertIn("from .base import AgentWorkflowTask", estimator_source)
        self.assertIn("async def estimate_task_fit", estimator_source)
        self.assertIn("def build_task_manifest", estimator_source)
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
        self.assertNotIn("ReviewResult", runner_source)
        self.assertNotIn("read_repo_file", runner_source)
        self.assertNotIn("TaskEstimate", runner_source)
        self.assertNotIn("AGENT_TASK_LIMITS", contracts_source)

    def test_agent_runtime_static_analysis_triggers_for_new_helper_package_files(self):
        quality_checks = load_quality_checks_module()

        self.assertTrue(
            quality_checks.QualityChecks.should_run_agent_runtime_static_analysis(
                ["scripts/private/workflow_action_update_agent/cli.py"],
            )
        )
        self.assertTrue(
            quality_checks.QualityChecks.should_run_agent_runtime_static_analysis(
                ["scripts/private/agent_runtime/openai_agent_runner.py"],
            )
        )
        self.assertFalse(
            quality_checks.QualityChecks.should_run_agent_runtime_static_analysis(
                ["scripts/private/unrelated_helper.py"],
            )
        )

    def test_workflow_action_update_agent_ownership_roots_have_agents_docs(self):
        helper_agents = HELPER_AGENTS_FILE.read_text(encoding="utf-8")
        workflow_agents = WORKFLOW_AUTOMATION_AGENTS_FILE.read_text(encoding="utf-8")

        self.assertIn("Workflow Action Update Agent helper commands", helper_agents)
        self.assertIn("agent_runtime.github_api", helper_agents)
        self.assertIn("agent_runtime.github_actions", helper_agents)
        self.assertIn("prompt templates", workflow_agents)
        self.assertIn(".github/agent-runtime/runtime/agent-models.json", workflow_agents)
        self.assertIn("scripts/private/workflow_action_update_agent/", workflow_agents)

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

    def test_task_estimator_uses_agent_workflow_task_contract(self):
        estimator = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_TASK_ESTIMATOR_SCRIPT,
            "agent_runtime.tasks.estimator_fake_contract",
        )
        agent_tasks = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_TASKS_SCRIPT,
            "agent_runtime.tasks.configured_fake_contract",
        )

        self.assertTrue(issubclass(estimator.TaskEstimatorWorkflowTask, estimator.AgentWorkflowTask))
        self.assertTrue(issubclass(agent_tasks.ConfiguredAgentWorkflowTask, agent_tasks.AgentWorkflowTask))
        self.assertTrue(issubclass(agent_tasks.ReviewAgentTask, agent_tasks.AgentWorkflowTask))
        self.assertTrue(issubclass(agent_tasks.RepositoryEditAgentTask, agent_tasks.AgentWorkflowTask))

    def test_openai_agent_runner_executes_simple_commands_without_shell_expansion(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_commands",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(Path(temp_dir), 10)
            output = repo_tools.run_shell_command('echo "$(git push)" && git diff --check')

        self.assertIn("$ echo '$(git push)'", output)
        self.assertIn("$(git push)", output)
        self.assertIn("$ git diff --check", output)

    def test_openai_agent_runner_executes_tokenized_pipelines_and_redirection(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_pipeline_commands",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(repo_root, 10)
            output = repo_tools.run_shell_command(
                "printf 'one\\ntwo\\n' | sed -n 2p > out.txt; cat < out.txt"
            )

            written_output = (repo_root / "out.txt").read_text(encoding="utf-8")

        self.assertEqual(written_output, "two\n")
        self.assertIn("$ printf 'one\\ntwo\\n' | sed -n 2p > out.txt", output)
        self.assertIn("$ cat < out.txt", output)
        self.assertIn("two", output)

    def test_openai_agent_runner_merges_stderr_into_stdout(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_stderr_redirection",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(Path(temp_dir), 10)
            output = repo_tools.run_shell_command("ls missing-workflow-agent-file 2>&1")

        self.assertIn("$ ls missing-workflow-agent-file 2>&1", output)
        self.assertIn("--- stdout ---", output)
        self.assertIn("missing-workflow-agent-file", output)
        self.assertIn("--- stderr ---\n", output)

    def test_openai_agent_runner_rejects_redirection_outside_repo(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_redirection_guard",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(Path(temp_dir) / "repo", 10)
            with self.assertRaisesRegex(ValueError, "escapes repository root"):
                repo_tools.run_shell_command("printf bad > ../outside.txt")

    def test_openai_agent_runner_rejects_git_metadata_reads(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_metadata_read_guard",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            git_dir = repo_root / ".git"
            git_dir.mkdir()
            (git_dir / "config").write_text("credential = unsafe\n", encoding="utf-8")
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(repo_root, 10)

            with self.assertRaisesRegex(ValueError, "Read path targets git metadata"):
                repo_tools.read_repo_file(".git/config")
            with self.assertRaisesRegex(ValueError, "Command argument targets git metadata"):
                repo_tools.run_shell_command("cat .git/config")
            with self.assertRaisesRegex(ValueError, "Shell redirection path targets git metadata"):
                repo_tools.run_shell_command("cat < .git/config")

    def test_openai_agent_runner_applies_safe_unified_diff(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_patch_guard_safe",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            target = repo_root / "example.txt"
            target.write_text("old\n", encoding="utf-8")
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(repo_root, 10)

            output = repo_tools.apply_unified_diff(
                textwrap.dedent(
                    """\
                    diff --git a/example.txt b/example.txt
                    --- a/example.txt
                    +++ b/example.txt
                    @@ -1 +1 @@
                    -old
                    +new
                    """
                )
            )

            self.assertIn("exit_code=0", output)
            self.assertEqual(target.read_text(encoding="utf-8"), "new\n")

    def test_openai_agent_runner_rejects_patch_paths_outside_safe_tree(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_patch_guard_reject",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir) / "repo"
            repo_root.mkdir()
            OPENAI_AGENT_RUNTIME_CONTEXT.set_run_context(repo_root, 10)

            git_metadata_patch = textwrap.dedent(
                """\
                diff --git a/.git/config b/.git/config
                --- a/.git/config
                +++ b/.git/config
                @@ -0,0 +1 @@
                +unsafe
                """
            )
            with self.assertRaisesRegex(ValueError, "Patch path targets git metadata"):
                repo_tools.apply_unified_diff(git_metadata_patch)

            escaping_patch = textwrap.dedent(
                """\
                diff --git a/../outside.txt b/../outside.txt
                --- a/../outside.txt
                +++ b/../outside.txt
                @@ -0,0 +1 @@
                +unsafe
                """
            )
            with self.assertRaisesRegex(ValueError, "escapes repository root"):
                repo_tools.apply_unified_diff(escaping_patch)

    def test_openai_agent_runner_blocks_mutating_git_commands_after_shell_splitting(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.tools.repo_fake_sdk_git_guards",
        )

        repo_tools.reject_unsafe_shell_command('echo "git push" && git diff --check')
        for command in (
            "git diff --check",
            "git show HEAD",
            "git log --oneline -1",
            "git status --short",
            "git ls-tree HEAD",
            "git grep agent-review",
            "git rev-parse HEAD",
            "git merge-base HEAD origin/main",
            "git cat-file -t HEAD",
            "git apply --check /tmp/example.patch",
            "git diff --check | sed -n 1,20p",
        ):
            repo_tools.reject_unsafe_shell_command(command)
        with self.assertRaisesRegex(ValueError, "git push"):
            repo_tools.reject_unsafe_shell_command('echo ok && git push')
        with self.assertRaisesRegex(ValueError, "git push"):
            repo_tools.reject_unsafe_shell_command("echo ok | git push")
        with self.assertRaisesRegex(ValueError, "Unsupported shell syntax"):
            repo_tools.reject_unsafe_shell_command("git diff --check || true")
        with self.assertRaisesRegex(ValueError, "Unsupported empty command"):
            repo_tools.reject_unsafe_shell_command("git diff --check |")
        with self.assertRaisesRegex(ValueError, "redirection requires a command"):
            repo_tools.reject_unsafe_shell_command("> out.txt")
        for command in (
            "git apply /tmp/example.patch",
            "git add -A",
            "git clean -fd",
            "git branch -D stale-branch",
            "git remote set-url origin https://example.invalid/repo.git",
            "git restore .",
            "git tag -d v0.0.0",
            "git config alias.publish push",
            "git diff --output=/tmp/diff.patch",
        ):
            with self.assertRaisesRegex(ValueError, "Command is intentionally blocked"):
                repo_tools.reject_unsafe_shell_command(command)

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

    def test_openai_agent_runner_blocks_prompt_that_exceeds_task_limit(self):
        estimator = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_TASK_ESTIMATOR_SCRIPT,
            "agent_runtime.tasks.estimator_fake_sdk_prompt_limit",
        )
        set_run_context = estimator.require_run_context.__globals__["set_run_context"]
        with tempfile.TemporaryDirectory() as temp_dir:
            set_run_context(Path(temp_dir), 10)
            settings = OPENAI_AGENT_TASK_CONFIG.resolve_agent_task_settings(
                AGENT_TASK_CONFIG_FILE,
                OPENAI_AGENT_CONTRACTS.AgentCommand.REPAIR,
                max_prompt_chars_override=10,
            )

            manifest = estimator.build_task_manifest(
                OPENAI_AGENT_CONTRACTS.AgentCommand.REPAIR,
                "x" * 11,
                settings,
                "gpt-test",
            )
            reasons = estimator.deterministic_task_limit_violations(manifest)

        self.assertIn("prompt has 11 characters", reasons[0])

    def test_openai_agent_runner_reports_review_diff_limits_as_advisory(self):
        estimator = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_TASK_ESTIMATOR_SCRIPT,
            "agent_runtime.tasks.estimator_fake_sdk_review_limit",
        )
        set_run_context = estimator.require_run_context.__globals__["set_run_context"]
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            subprocess.run(["git", "init"], cwd=repo_root, check=True, capture_output=True)
            subprocess.run(
                ["git", "config", "user.email", "agent@example.invalid"],
                cwd=repo_root,
                check=True,
            )
            subprocess.run(["git", "config", "user.name", "Agent"], cwd=repo_root, check=True)
            (repo_root / "README.md").write_text("base\n", encoding="utf-8")
            subprocess.run(["git", "add", "README.md"], cwd=repo_root, check=True)
            subprocess.run(["git", "commit", "-m", "base"], cwd=repo_root, check=True, capture_output=True)
            base_sha = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo_root, text=True).strip()

            for index in range(3):
                (repo_root / f"file-{index}.txt").write_text(f"{index}\n", encoding="utf-8")
            subprocess.run(["git", "add", "."], cwd=repo_root, check=True)
            subprocess.run(["git", "commit", "-m", "change"], cwd=repo_root, check=True, capture_output=True)
            head_sha = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo_root, text=True).strip()

            set_run_context(repo_root, 10)
            settings = OPENAI_AGENT_TASK_CONFIG.resolve_agent_task_settings(
                AGENT_TASK_CONFIG_FILE,
                OPENAI_AGENT_CONTRACTS.AgentCommand.REVIEW,
                max_review_files_override=2,
                max_review_changed_lines_override=2,
            )
            prompt = f"- Base SHA: `{base_sha}`\n- Head SHA: `{head_sha}`\n"
            manifest = estimator.build_task_manifest(
                OPENAI_AGENT_CONTRACTS.AgentCommand.REVIEW,
                prompt,
                settings,
                "gpt-test",
            )
            reasons = estimator.deterministic_task_limit_violations(manifest)
            estimate = types.SimpleNamespace(
                fits=True,
                estimated_turns=1,
                reason="Review can continue.",
                split_recommendation=None,
            )
            advisory_reasons = estimator.task_estimate_advisory_reasons(manifest, cast(Any, estimate))

        self.assertEqual(reasons, [])
        self.assertIn("review scope touches 3 files", advisory_reasons[0])
        self.assertIn("review scope changes 3 lines", advisory_reasons[1])

    def test_openai_agent_runner_reports_estimated_turn_overrun_as_advisory(self):
        estimator = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_TASK_ESTIMATOR_SCRIPT,
            "agent_runtime.tasks.estimator_fake_sdk_turn_limit",
        )
        manifest = {
            "prompt_chars": 10,
            "limits": {
                "max_turns": OPENAI_REVIEW_MAX_TURNS,
                "max_prompt_chars": 100,
                "max_review_files": None,
                "max_review_changed_lines": None,
            },
            "git_metrics": {
                "total_diff_files": 0,
                "total_diff_changed_lines": 0,
            },
        }
        estimate = types.SimpleNamespace(
            fits=True,
            estimated_turns=OPENAI_REVIEW_MAX_TURNS + 1,
            reason="Needs more exploration.",
            split_recommendation="Split by workflow.",
        )

        block_reasons = estimator.task_estimate_block_reasons(manifest)
        advisory_reasons = estimator.task_estimate_advisory_reasons(manifest, cast(Any, estimate))

        self.assertEqual(block_reasons, [])
        self.assertEqual(
            advisory_reasons,
            [f"estimator expects {OPENAI_REVIEW_MAX_TURNS + 1} turns, above the {OPENAI_REVIEW_MAX_TURNS} turn limit"],
        )

    def test_agent_review_output_drops_invalid_right_side_anchors(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            workflow_path = repo_root / ".github/workflows/workflow-audit.yml"
            workflow_path.parent.mkdir(parents=True)
            workflow_path.write_text("name: Workflow audit\njobs: {}\n", encoding="utf-8")

            payload = {
                "summary": "Reviewed workflow changes.",
                "overall_recommendation": "request_changes",
                "overall_score": 0.92,
                "overall_confidence": 0.87,
                "findings": [
                    {
                        "title": "Impossible stale workflow path",
                        "severity": "major",
                        "score": 0.92,
                        "confidence": 0.94,
                        "path": ".github/workflows/workflow-audit.yml",
                        "diff_side": "RIGHT",
                        "start_line": 367,
                        "end_line": 367,
                        "body": "This line does not exist in the current checkout.",
                        "suggestion": None,
                    },
                    {
                        "title": "Supported note",
                        "severity": "note",
                        "score": 0.32,
                        "confidence": 0.81,
                        "path": ".github/workflows/workflow-audit.yml",
                        "diff_side": "RIGHT",
                        "start_line": 1,
                        "end_line": 1,
                        "body": "This line exists in the current checkout.",
                        "suggestion": None,
                    },
                ],
            }

            filtered = AGENT_REVIEW_OUTPUT.filter_invalid_right_side_findings(payload, repo_root)

        self.assertEqual(filtered["overall_recommendation"], "comment")
        self.assertEqual(filtered["overall_score"], 0.32)
        self.assertEqual(
            filtered["summary"],
            (
                "Review kept 1 supported non-blocking finding. "
                "Omitted 1 unsupported RIGHT-side finding whose anchors are not supported by the current checkout."
            ),
        )
        self.assertEqual([finding["title"] for finding in filtered["findings"]], ["Supported note"])

    def test_agent_review_output_drops_known_available_action_ref_claims(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            workflow_path = repo_root / ".github/workflows/agent-review.yml"
            workflow_path.parent.mkdir(parents=True)
            workflow_path.write_text("uses: actions/checkout@v6\n", encoding="utf-8")

            payload = {
                "summary": "Reviewed workflow changes.",
                "overall_recommendation": "request_changes",
                "overall_score": 0.9,
                "overall_confidence": 0.96,
                "findings": [
                    {
                        "title": "Checkout action is non-existent",
                        "severity": "major",
                        "score": 0.9,
                        "confidence": 0.96,
                        "path": ".github/workflows/agent-review.yml",
                        "diff_side": "RIGHT",
                        "start_line": 1,
                        "end_line": 1,
                        "body": "The currently published major version is v4; actions/checkout@v6 is non-existent.",
                        "suggestion": "uses: actions/checkout@v4",
                    },
                ],
            }

            filtered = AGENT_REVIEW_OUTPUT.filter_invalid_right_side_findings(payload, repo_root)

        self.assertEqual(filtered["overall_recommendation"], "approve")
        self.assertEqual(filtered["findings"], [])

    def test_agent_review_output_drops_verified_model_availability_claims(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            model_config_path = repo_root / ".github/agent-runtime/runtime/agent-models.json"
            model_config_path.parent.mkdir(parents=True)
            model_config_path.write_text(
                json.dumps(
                    {
                        "default_agent_model": "gpt-5.5",
                        "agents": {
                            "review": {"model": "gpt-5.5"},
                            "repair": {"model": "gpt-5.5"},
                            "stabilization": {"model": "gpt-5.5"},
                        },
                    },
                    indent=2,
                ),
                encoding="utf-8",
            )

            payload = {
                "summary": "Reviewed workflow changes.",
                "overall_recommendation": "request_changes",
                "overall_score": 0.78,
                "overall_confidence": 0.86,
                "findings": [
                    {
                        "title": "Default agent model is set to an unavailable-looking future model",
                        "severity": "major",
                        "score": 0.78,
                        "confidence": 0.86,
                        "path": ".github/agent-runtime/runtime/agent-models.json",
                        "diff_side": "RIGHT",
                        "start_line": 1,
                        "end_line": 12,
                        "body": (
                            "All agent instances now default to `gpt-5.5`, so jobs will fail at runtime "
                            "unless the proxy exposes this exact model."
                        ),
                        "suggestion": None,
                    },
                ],
            }

            unverified = AGENT_REVIEW_OUTPUT.filter_invalid_right_side_findings(payload, repo_root)
            filtered = AGENT_REVIEW_OUTPUT.filter_invalid_right_side_findings(
                payload,
                repo_root,
                verified_model="gpt-5.5",
            )

        self.assertEqual(unverified, payload)
        self.assertEqual(filtered["overall_recommendation"], "approve")
        self.assertEqual(filtered["findings"], [])

    def test_agent_review_output_drops_verified_requirement_availability_claims(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            requirements_path = repo_root / ".github/agent-runtime/runtime/requirements-openai-agents.txt"
            requirements_path.parent.mkdir(parents=True)
            requirements_path.write_text(
                "\n".join(
                    [
                        "openai-agents==0.17.7",
                        "openai==2.44.0",
                        "pydantic==2.13.4",
                        "truststore==0.10.4",
                    ]
                )
                + "\n",
                encoding="utf-8",
            )
            installed_versions = {
                "openai-agents": "0.17.7",
                "openai": "2.44.0",
                "pydantic": "2.13.4",
            }

            payload = {
                "summary": "Reviewed workflow changes.",
                "overall_recommendation": "request_changes",
                "overall_score": 0.78,
                "overall_confidence": 0.86,
                "findings": [
                    {
                        "title": "Pinned OpenAI runtime dependencies are unavailable",
                        "severity": "major",
                        "score": 0.78,
                        "confidence": 0.86,
                        "path": ".github/agent-runtime/runtime/requirements-openai-agents.txt",
                        "diff_side": "RIGHT",
                        "start_line": 1,
                        "end_line": 3,
                        "body": (
                            "openai-agents==0.17.7, openai==2.44.0, and pydantic==2.13.4 "
                            "are not valid published versions, so pip install will fail during "
                            "dependency installation."
                        ),
                        "suggestion": None,
                    },
                ],
            }

            with mock.patch.object(
                AGENT_REVIEW_OUTPUT.importlib_metadata,
                "version",
                side_effect=lambda name: installed_versions[name],
            ):
                filtered = AGENT_REVIEW_OUTPUT.filter_invalid_right_side_findings(payload, repo_root)

        self.assertEqual(filtered["overall_recommendation"], "approve")
        self.assertEqual(filtered["findings"], [])

    def test_agent_review_output_drops_verified_agent_runtime_artifact_context_claims(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            prompt_path = repo_root / ".github/agent-runtime/workflow-action-update-agent/prompts/repair-goal.md.in"
            prompt_path.parent.mkdir(parents=True)
            prompt_path.write_text(
                "\n".join(
                    [
                        "Read the generated context files first.",
                        "Then inspect `.agent-runtime/workflow-action-update-agent/source-run.log`.",
                        "Relevant text artifacts are under `.agent-runtime/workflow-action-update-agent/artifacts/`.",
                    ]
                ),
                encoding="utf-8",
            )

            payload = {
                "summary": "Reviewed workflow changes.",
                "overall_recommendation": "request_changes",
                "overall_score": 0.7,
                "overall_confidence": 0.9,
                "findings": [
                    {
                        "title": "Optional source artifacts are downloaded outside the agent context",
                        "severity": "major",
                        "score": 0.7,
                        "confidence": 0.9,
                        "path": ".github/agent-runtime/workflow-action-update-agent/prompts/repair-goal.md.in",
                        "diff_side": "RIGHT",
                        "start_line": 3,
                        "end_line": 3,
                        "body": (
                            "The artifact is outside the agent context, but omit the finding if "
                            "`.agent-runtime/workflow-action-update-agent/artifacts/...` is matched."
                        ),
                        "suggestion": None,
                    },
                ],
            }

            filtered = AGENT_REVIEW_OUTPUT.filter_invalid_right_side_findings(payload, repo_root)

        self.assertEqual(filtered["overall_recommendation"], "approve")
        self.assertEqual(filtered["findings"], [])

    def test_agent_review_output_keeps_artifact_context_claims_without_anchor_evidence(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            prompt_path = repo_root / ".github/agent-runtime/workflow-action-update-agent/prompts/repair-goal.md.in"
            prompt_path.parent.mkdir(parents=True)
            prompt_path.write_text(
                "\n".join(
                    [
                        "Read the generated context files first.",
                        "Then inspect `.agent-runtime/workflow-action-update-agent/source-run.log`.",
                    ]
                ),
                encoding="utf-8",
            )

            payload = {
                "summary": "Reviewed workflow changes.",
                "overall_recommendation": "request_changes",
                "overall_score": 0.7,
                "overall_confidence": 0.9,
                "findings": [
                    {
                        "title": "Optional source artifacts are downloaded outside the agent context",
                        "severity": "major",
                        "score": 0.7,
                        "confidence": 0.9,
                        "path": ".github/agent-runtime/workflow-action-update-agent/prompts/repair-goal.md.in",
                        "diff_side": "RIGHT",
                        "start_line": 2,
                        "end_line": 2,
                        "body": (
                            "The artifact is outside the agent context, but omit the finding if "
                            "`.agent-runtime/workflow-action-update-agent/artifacts/...` is matched."
                        ),
                        "suggestion": None,
                    },
                ],
            }

            filtered = AGENT_REVIEW_OUTPUT.filter_invalid_right_side_findings(payload, repo_root)

        self.assertEqual(filtered, payload)

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
        requirements = AGENT_STATIC_REQUIREMENTS_FILE.read_text(encoding="utf-8").splitlines()

        self.assertEqual(
            set(requirements),
            {
                "mypy==1.16.1",
                "pyflakes==3.3.2",
                "types-PyYAML==6.0.12.20250516",
                "vulture==2.14",
            },
        )

    def test_agent_static_analysis_derives_removed_paths_from_name_status(self):
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
            OPENAI_AGENT_STATIC_ANALYSIS.parse_removed_or_renamed_paths(name_status),
            ["tools/retired-runner.py", "scripts/private/unused_helper.py"],
        )
        self.assertEqual(
            OPENAI_AGENT_STATIC_ANALYSIS.reference_tokens_for_removed_path("tools/retired-runner.py"),
            {"tools/retired-runner.py", "tools/retired-runner"},
        )

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
        self.assertEqual(
            OPENAI_AGENT_MODEL_CONFIG.resolve_agent_model(
                AGENT_MODEL_CONFIG_FILE,
                "review",
                override_model="gpt-override",
            ),
            "gpt-override",
        )

    def test_agent_tasks_are_centrally_configured_per_command(self):
        task_config = json.loads(AGENT_TASK_CONFIG_FILE.read_text(encoding="utf-8"))

        self.assertEqual(set(task_config["tasks"]), {"run-review", "run-repair", "run-stabilization"})
        self.assertEqual(task_config["tasks"]["run-review"]["agent_instance"], "review")
        self.assertEqual(task_config["tasks"]["run-review"]["max_turns"], OPENAI_REVIEW_MAX_TURNS)
        self.assertEqual(task_config["tasks"]["run-review"]["task_estimate_turns"], 3)
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

    def test_local_review_runner_uses_shared_sdk_script(self):
        content = AGENT_REVIEW_RUN_SCRIPT.read_text(encoding="utf-8")

        self.assertIn('"AGENT_REVIEW_AGENT_VENV", ".agent-runtime/openai-agent-venv"', content)
        self.assertIn('os.environ["REVIEW_BASE_REF"] = args.base_ref', content)
        self.assertIn('os.environ.setdefault("REVIEW_HEAD_REF", "HEAD")', content)
        self.assertIn('os.environ.setdefault("REVIEW_REPOSITORY", "local-checkout")', content)
        self.assertIn('"AGENT_REVIEW_PYTHON"', content)
        self.assertIn('"AGENT_RUNTIME_PYTHON", "python3"', content)
        self.assertIn("Agent review requires Python 3.10 or newer", content)
        self.assertIn('run_command([agent_python, "-m", "venv", str(agent_venv)])', content)
        self.assertIn(
            '".github/agent-runtime/runtime/requirements-openai-agents.txt"',
            content,
        )
        self.assertIn("run-review", content)
        self.assertIn(
            '"scripts/private/agent_runtime/openai_agent_runner.py"',
            content,
        )
        self.assertNotIn("--command run-review", content)
        self.assertNotIn("--agent-instance review", content)
        self.assertNotIn("--model-config-file .github/agent-runtime/runtime/agent-models.json", content)
        self.assertNotIn("--task-config-file .github/agent-runtime/runtime/agent-tasks.json", content)
        self.assertIn('if os.environ.get("AGENT_REVIEW_MODEL"):', content)
        self.assertIn('agent_args.extend(["--model", os.environ["AGENT_REVIEW_MODEL"]])', content)
        self.assertNotIn("${AGENT_MODEL", content)
        self.assertIn('".github/agent-runtime/review/schemas/review.schema.json"', content)
        self.assertIn('"scripts/private/agent_runtime/review/publish.py"', content)
        self.assertNotIn("pip install --user", content)
        self.assertNotIn("OPENAI_API_KEY=", content)

    def test_review_runtime_assets_do_not_own_scripts(self):
        self.assertFalse((AGENT_REVIEW_ROOT / "scripts").exists())
        self.assertTrue(AGENT_REVIEW_PROMPT_SCRIPT.is_file())
        self.assertTrue(AGENT_REVIEW_FETCH_SCRIPT.is_file())
        self.assertTrue(AGENT_REVIEW_PUBLISH_SCRIPT.is_file())
        self.assertTrue(AGENT_REVIEW_RUN_SCRIPT.is_file())

    def test_agent_review_prompt_renderer_uses_runtime_context(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_path = Path(temp_dir)
            template = temp_path / "review.md.in"
            output = temp_path / "review.prompt.md"
            template.write_text(
                "\n".join(
                    [
                        "@@REPOSITORY@@",
                        "@@BASE_REF@@",
                        "@@BASE_SHA@@",
                        "@@HEAD_SHA@@",
                        "@@PR_NUMBER@@",
                        "@@PR_TITLE@@",
                        "@@PR_URL@@",
                    ]
                ),
                encoding="utf-8",
            )

            with mock.patch.dict(
                os.environ,
                {
                    "REVIEW_REPOSITORY": "Arm-Debug/amp-dev-forge",
                    "REVIEW_BASE_REF": "origin/main",
                    "REVIEW_BASE_SHA": "base-sha",
                    "REVIEW_HEAD_SHA": "head-sha",
                    "REVIEW_PR_NUMBER": "175",
                    "REVIEW_PR_TITLE": "Line one\nline two",
                    "REVIEW_PR_URL": "https://github.com/Arm-Debug/amp-dev-forge/pull/175",
                },
                clear=False,
            ):
                AGENT_REVIEW_PROMPT.render_prompt(output_path=output, template_path=template)

            rendered = output.read_text(encoding="utf-8")

        self.assertIn("Arm-Debug/amp-dev-forge", rendered)
        self.assertIn("origin/main", rendered)
        self.assertIn("base-sha", rendered)
        self.assertIn("head-sha", rendered)
        self.assertIn("175", rendered)
        self.assertIn("Line one line two", rendered)
        self.assertIn("https://github.com/Arm-Debug/amp-dev-forge/pull/175", rendered)

    def test_agent_review_prompt_omits_unsupported_or_contradicted_claims(self):
        content = AGENT_REVIEW_PROMPT_TEMPLATE.read_text(encoding="utf-8")

        self.assertIn("Prefer complete coverage of concrete, verified issues", content)
        self.assertIn("unsupported or contradicted by the current checkout, omit it", content)
        self.assertIn("prefer omission over unsupported or contradicted findings", content)
        self.assertIn("<agent-review:suppress>", content)
        self.assertIn("<agent-review:suppress-begin>", content)
        self.assertNotIn("under-reporting is worse", content)

    def test_standard_validation_workflows_accept_manual_pr_context(self):
        pek_ci = load_yaml(PEK_CI_WORKFLOW_FILE)
        sonar = load_yaml(SONAR_WORKFLOW_FILE)
        pek_inputs = pek_ci["on"]["workflow_dispatch"]["inputs"]
        sonar_inputs = sonar["on"]["workflow_dispatch"]["inputs"]
        pek_steps = step_map(pek_ci["jobs"]["quality-checks"])
        sonar_steps = step_map(sonar["jobs"]["build-and-sonar"])
        linux_checkout = pek_ci["jobs"]["linux-quick-start-build-test"]["steps"][0]
        rpi_checkout = pek_ci["jobs"]["rpi5-quick-start-build-test"]["steps"][0]
        expected_label_gate = "github.event.action != 'labeled' || github.event.label.name == 'run-pek-ci'"
        expected_draft_override = "github.event.action == 'labeled' && github.event.label.name == 'run-pek-ci'"

        self.assertEqual(
            set(pek_inputs.keys()),
            {"pr_number", "pr_base_ref", "pr_head_ref", "pr_head_sha"},
        )
        self.assertEqual(
            set(sonar_inputs.keys()),
            {"pr_number", "pr_base_ref", "pr_head_ref", "pr_head_sha"},
        )
        self.assertIn("Resolve manual PR context", pek_steps)
        self.assertIn("Resolve manual PR context", sonar_steps)
        for job_name in ("linux-quick-start-build-test", "rpi5-quick-start-build-test", "quality-checks"):
            job_condition = pek_ci["jobs"][job_name]["if"]
            self.assertIn(expected_label_gate, job_condition)
            self.assertIn(expected_draft_override, job_condition)
            self.assertNotIn("contains(github.event.label.name, 'run-pek-ci')", job_condition)
        sonar_condition = sonar["jobs"]["build-and-sonar"]["if"]
        self.assertIn(expected_label_gate, sonar_condition)
        self.assertIn(expected_draft_override, sonar_condition)
        self.assertNotIn("contains(github.event.label.name, 'run-pek-ci')", sonar_condition)
        for checkout_step in (linux_checkout, rpi_checkout):
            checkout_ref = checkout_step["with"]["ref"]
            self.assertIn("inputs.pr_head_sha", checkout_ref)
            self.assertIn("inputs.pr_head_ref", checkout_ref)
            self.assertIn("github.head_ref", checkout_ref)
        self.assertIn("inputs.pr_head_sha", pek_steps["Checkout"]["with"]["ref"])
        self.assertIn("steps.manual_pr.outputs.head_sha", pek_steps["Checkout"]["with"]["ref"])
        self.assertIn("inputs.pr_head_sha", sonar_steps["Checkout"]["with"]["ref"])
        self.assertIn("steps.manual_pr.outputs.head_sha", sonar_steps["Checkout"]["with"]["ref"])
        self.assertIn(
            "steps.manual_pr.outputs.base_ref",
            pek_steps["Check Repo Quality gate (PR)"]["run"],
        )
        self.assertIn("inputs.pr_number", sonar_steps["SonarQube analysis"]["env"]["PR_KEY"])
        self.assertIn("steps.manual_pr.outputs.base_ref", sonar_steps["SonarQube analysis"]["env"]["PR_BASE"])

    def test_stabilizer_workflow_uses_canonical_agent_review_shape(self):
        workflow = load_yaml(STABILIZER_WORKFLOW_FILE)
        call_inputs = workflow["on"]["workflow_call"]["inputs"]
        dispatch_inputs = workflow["on"]["workflow_dispatch"]["inputs"]
        job = workflow["jobs"]["stabilize"]
        steps = step_map(job)

        self.assertEqual(set(call_inputs.keys()), set(dispatch_inputs.keys()))
        self.assertEqual(job["runs-on"], OPENAI_AGENT_RUNNER_LABEL)
        self.assertEqual(job["permissions"]["actions"], "read")
        self.assertEqual(steps["Checkout workflow helpers"]["with"]["persist-credentials"], "false")
        self.assertEqual(steps["Checkout PR head"]["with"]["persist-credentials"], "false")
        self.assertEqual(
            list(steps),
            [
                "Checkout workflow helpers",
                "Snapshot workflow helper bundle",
                "Resolve PR details",
                "Checkout PR head",
                "Restore workflow helper bundle",
                "Prepare stabilization context",
                "Set up Agent Python",
                "Install OpenAI agent runtime",
                "Run OpenAI SDK stabilization agent",
                "Run stabilization validation",
                "Commit stabilization fix",
                "Write stabilization skip artifact",
                "Upload stabilization artifacts",
            ],
        )
        snapshot_step = steps["Snapshot workflow helper bundle"]
        self.assertIn('cp -R scripts/private/agent_runtime/.', snapshot_step["run"])
        self.assertIn("cp .github/agent-runtime/runtime/requirements-openai-agents.txt", snapshot_step["run"])
        self.assertIn("cp .github/agent-runtime/runtime/agent-models.json", snapshot_step["run"])
        self.assertIn("cp .github/agent-runtime/runtime/agent-tasks.json", snapshot_step["run"])
        self.assertIn(
            'cp -R .github/agent-runtime/review/prompts/. "${bundle_root}/.github/agent-runtime/review/prompts"',
            snapshot_step["run"],
        )
        self.assertIn(
            'cp -R .github/agent-runtime/review/schemas/. "${bundle_root}/.github/agent-runtime/review/schemas"',
            snapshot_step["run"],
        )
        self.assertIn(
            'cp -R .github/agent-runtime/workflow-action-update-agent/prompts/. "${bundle_root}/.github/agent-runtime/workflow-action-update-agent/prompts"',
            snapshot_step["run"],
        )
        self.assertIn(
            'cp -R .github/agent-runtime/workflow-action-update-agent/profiles/. "${bundle_root}/.github/agent-runtime/workflow-action-update-agent/profiles"',
            snapshot_step["run"],
        )
        self.assertNotIn('cp -R agent-review/. "${bundle_root}/agent-review"', snapshot_step["run"])
        self.assertNotIn(".github/agent-runtime/review/out", snapshot_step["run"])
        self.assertNotIn('cp -R scripts/private/. "${bundle_root}/scripts/private"', snapshot_step["run"])
        python_step = steps["Set up Agent Python"]
        self.assertEqual(python_step["if"], "${{ steps.context.outputs.review_recommendation != 'approve' }}")
        self.assertEqual(python_step["uses"], "actions/setup-python@v6")
        self.assertEqual(python_step["with"]["python-version"], "3.10")
        install_step = steps["Install OpenAI agent runtime"]
        self.assertEqual(install_step["shell"], "bash")
        self.assertIn("python3 -m venv .agent-runtime/openai-agent-venv", install_step["run"])
        self.assertIn(
            ".agent-runtime/openai-agent-venv/bin/python -m pip install -r .workflow-action-update-agent-helper/.github/agent-runtime/runtime/requirements-openai-agents.txt",
            install_step["run"],
        )
        agent_step = steps["Run OpenAI SDK stabilization agent"]
        self.assertEqual(agent_step["shell"], "bash")
        self.assertEqual(
            agent_step["env"]["OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS"],
            "${{ secrets.OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS }}",
        )
        self.assertIn(
            ".agent-runtime/openai-agent-venv/bin/python .workflow-action-update-agent-helper/scripts/private/agent_runtime/openai_agent_runner.py run-stabilization",
            agent_step["run"],
        )
        self.assertIn(
            "--model-config-file "
            ".workflow-action-update-agent-helper/.github/agent-runtime/runtime/agent-models.json",
            agent_step["run"],
        )
        self.assertIn(
            "--task-config-file "
            ".workflow-action-update-agent-helper/.github/agent-runtime/runtime/agent-tasks.json",
            agent_step["run"],
        )
        self.assertIn('--prompt-file "${{ inputs.context_root }}/stabilize-goal.md"', agent_step["run"])
        self.assertIn(
            '--output-file "${{ runner.temp }}/workflow-action-update-agent-stabilize-output.md"',
            agent_step["run"],
        )
        self.assertNotIn("--agent-instance", agent_step["run"])
        self.assertNotIn("--max-turns", agent_step["run"])
        self.assertEqual(
            steps["Resolve PR details"]["with"]["command"],
            "resolve-pr-details",
        )
        self.assertEqual(snapshot_step["shell"], "bash")
        restore_step = steps["Restore workflow helper bundle"]
        self.assertEqual(restore_step["shell"], "bash")
        self.assertIn('mkdir -p "${helper_root}"', restore_step["run"])
        self.assertIn('cp -R "${bundle_root}/." "${helper_root}"', restore_step["run"])
        self.assertEqual(
            steps["Prepare stabilization context"]["with"]["command"],
            "prepare-stabilization-context",
        )
        self.assertEqual(
            steps["Prepare stabilization context"]["with"]["profile-path"],
            ".workflow-action-update-agent-helper/${{ inputs.profile_path }}",
        )
        self.assertEqual(
            steps["Prepare stabilization context"]["uses"],
            "./.workflow-action-update-agent-helper/.github/actions/workflow-action-update-agent-helper",
        )
        validation_step = steps["Run stabilization validation"]
        self.assertEqual(validation_step["shell"], "bash")
        self.assertNotIn("uses", validation_step)
        self.assertEqual(validation_step["env"]["GH_TOKEN"], "")
        self.assertEqual(validation_step["env"]["GITHUB_TOKEN"], "")
        self.assertEqual(validation_step["env"]["OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS"], "")
        self.assertEqual(validation_step["env"]["OPENAI_API_KEY"], "")
        self.assertIn(
            "python3 -m workflow_action_update_agent run-validation",
            validation_step["run"],
        )
        self.assertIn(
            '--profile-path "${RUNNER_TEMP}/workflow-action-update-agent-helper/${{ inputs.profile_path }}"',
            validation_step["run"],
        )
        commit_step = steps["Commit stabilization fix"]
        self.assertEqual(commit_step["shell"], "bash")
        self.assertNotIn("uses", commit_step)
        self.assertIn(
            "python3 -m workflow_action_update_agent commit-review-fix",
            commit_step["run"],
        )
        self.assertIn('--context-root "${{ inputs.context_root }}"', commit_step["run"])
        self.assertIn('--pr-number "${{ inputs.pr_number }}"', commit_step["run"])
        skip_step = steps["Write stabilization skip artifact"]
        self.assertEqual(skip_step["if"], "${{ steps.context.outputs.review_recommendation == 'approve' }}")
        self.assertIn('mkdir -p "${{ runner.temp }}"', skip_step["run"])
        self.assertIn("workflow-action-update-agent-stabilize-output.md", skip_step["run"])
        self.assertIn("No stabilization agent run was needed", skip_step["run"])
        self.assertEqual(
            commit_step["env"]["GH_TOKEN"],
            "${{ secrets.EXPKITS_AGENT_TOKEN }}",
        )

    def test_resolve_inputs_uses_profile_branch_template(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            output_file = Path(temp_dir) / "outputs.txt"
            args = argparse.Namespace(
                profile_path=str(PROFILE_FILE),
                source_run_id="12345",
                target_branch="",
                ticket_id="EXPKITS-4242",
                current_ref_name="main",
                github_output=str(output_file),
            )
            run_payload = {
                "html_url": "https://github.com/Arm-Debug/amp-dev-forge/actions/runs/12345",
                "name": "Perception Experience Kit CI Pipeline",
                "conclusion": "failure",
                "head_branch": "feature/example/topic",
                "head_repository": {"full_name": "Arm-Debug/amp-dev-forge"},
                "pull_requests": [{"number": 169}],
            }

            with mock.patch.object(
                OPENAI_AGENT_GITHUB_ACTIONS,
                "github_api_json",
                side_effect=[
                    run_payload,
                    {"labels": [{"name": "agent-autorepair"}]},
                ],
            ):
                with mock.patch.dict(os.environ, {"GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge"}, clear=False):
                    result = HELPER_REPAIR.command_resolve_inputs(args)

            self.assertEqual(result, 0)
            outputs = dict(
                line.split("=", 1)
                for line in output_file.read_text(encoding="utf-8").splitlines()
                if line.strip()
            )
            self.assertEqual(outputs["should_run"], "true")
            self.assertEqual(outputs["source_pr_number"], "169")
            self.assertEqual(outputs["target_branch"], "feature/example/topic")
            self.assertEqual(
                outputs["repair_branch"],
                REPAIR_BRANCH,
            )
            self.assertEqual(outputs["agent_model"], "gpt-5.5")

    def test_resolve_inputs_requires_source_pr_authorization_label(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            output_file = Path(temp_dir) / "outputs.txt"
            args = argparse.Namespace(
                profile_path=str(PROFILE_FILE),
                source_run_id="12345",
                target_branch="",
                ticket_id="EXPKITS-4242",
                current_ref_name="main",
                github_output=str(output_file),
            )
            run_payload = {
                "html_url": "https://github.com/Arm-Debug/amp-dev-forge/actions/runs/12345",
                "name": "Perception Experience Kit CI Pipeline",
                "conclusion": "failure",
                "head_branch": "feature/example/topic",
                "head_repository": {"full_name": "Arm-Debug/amp-dev-forge"},
                "pull_requests": [{"number": 169}],
            }

            with mock.patch.object(
                OPENAI_AGENT_GITHUB_ACTIONS,
                "github_api_json",
                side_effect=[
                    run_payload,
                    {"labels": [{"name": "run-pek-ci"}]},
                ],
            ):
                with mock.patch.dict(os.environ, {"GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge"}, clear=False):
                    result = HELPER_REPAIR.command_resolve_inputs(args)

            self.assertEqual(result, 0)
            outputs = dict(
                line.split("=", 1)
                for line in output_file.read_text(encoding="utf-8").splitlines()
                if line.strip()
            )
            self.assertEqual(outputs["should_run"], "false")
            self.assertEqual(outputs["source_pr_number"], "")
            self.assertEqual(outputs["repair_branch"], "")
            self.assertIn("agent-autorepair", outputs["skip_reason"])

    def test_resolve_inputs_requires_source_pull_request(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            output_file = Path(temp_dir) / "outputs.txt"
            args = argparse.Namespace(
                profile_path=str(PROFILE_FILE),
                source_run_id="12345",
                target_branch="",
                ticket_id="EXPKITS-4242",
                current_ref_name="main",
                github_output=str(output_file),
            )
            run_payload = {
                "html_url": "https://github.com/Arm-Debug/amp-dev-forge/actions/runs/12345",
                "name": "Perception Experience Kit CI Pipeline",
                "conclusion": "failure",
                "head_branch": "feature/example/topic",
                "head_repository": {"full_name": "Arm-Debug/amp-dev-forge"},
                "pull_requests": [],
            }

            with mock.patch.object(OPENAI_AGENT_GITHUB_ACTIONS, "github_api_json", return_value=run_payload):
                with mock.patch.dict(os.environ, {"GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge"}, clear=False):
                    result = HELPER_REPAIR.command_resolve_inputs(args)

            self.assertEqual(result, 0)
            outputs = dict(
                line.split("=", 1)
                for line in output_file.read_text(encoding="utf-8").splitlines()
                if line.strip()
            )
            self.assertEqual(outputs["should_run"], "false")
            self.assertEqual(outputs["skip_reason"], "Source workflow run is not associated with a pull request.")

    def test_audit_profile_allows_non_failure_source_run_and_configures_validation(self):
        audit_profile = HELPER_RUNTIME.load_profile(str(WORKFLOW_AUDIT_PROFILE_FILE))

        self.assertFalse(HELPER_RUNTIME.profile_bool(audit_profile, "require_failure_conclusion", True))
        self.assertEqual(
            audit_profile["repair_branch_template"],
            "feature/{ticket_id}/bot-workflow-dependency-freshness-{source_run_id}",
        )
        self.assertEqual(audit_profile["repair_authorization_label"], "agent-autorepair")
        self.assertEqual(audit_profile["pr_trigger_label"], "run-pek-ci")
        validation_workflows = audit_profile["validation_workflows"]
        self.assertEqual(
            [item["workflow_file"] for item in validation_workflows],
            ["agent-review.yml", "workflow-audit.yml", "pek-ci.yml", "sonar.yml"],
        )

    def test_agent_review_gate_is_profile_driven(self):
        profile = HELPER_RUNTIME.load_profile(str(PROFILE_FILE))
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

    def test_profile_drives_markdown_context_files_and_validation_commands(self):
        profile = HELPER_RUNTIME.load_profile(str(PROFILE_FILE))

        with tempfile.TemporaryDirectory() as temp_dir:
            context_root = Path(temp_dir) / "context"
            artifact_root = context_root / "artifacts"
            artifact_root.mkdir(parents=True)
            (context_root / "source-run.json").write_text("{}\n", encoding="utf-8")
            (context_root / "source-run.log").write_text("log\n", encoding="utf-8")
            (artifact_root / "summary.txt").write_text("artifact\n", encoding="utf-8")

            args = argparse.Namespace(
                profile_path=str(PROFILE_FILE),
                context_root=str(context_root),
                source_run_id="12345",
                source_pr_number="169",
                source_run_url="https://github.com/Arm-Debug/amp-dev-forge/actions/runs/12345",
                source_workflow_name="Perception Experience Kit CI Pipeline",
                target_branch="main",
                repair_branch=REPAIR_BRANCH,
                ticket_id="EXPKITS-4242",
            )

            result = HELPER_REPAIR.command_build_markdown(args)

            self.assertEqual(result, 0)
            goal = (context_root / "goal.md").read_text(encoding="utf-8")
            validation = (context_root / "validation.md").read_text(encoding="utf-8")
            inventory = (context_root / "file-inventory.md").read_text(encoding="utf-8")

            self.assertIn("# Workflow Action Update Agent", goal)
            self.assertIn("explicitly authorized on the source PR", goal)
            self.assertTrue(CONTEXT_TEMPLATE.is_file())
            failure_context = (context_root / "failure-context.md").read_text(encoding="utf-8")
            self.assertIn("Source PR: #169", failure_context)
            self.assertIn("Required source PR authorization label: `agent-autorepair`", failure_context)
            for path in profile["prompt_context_files"]:
                self.assertIn(f"- `{path}`", goal)
            for command in profile["validation_commands"]:
                self.assertIn(f"- `{command}`", validation)
            self.assertIn("- `artifacts/summary.txt`", inventory)

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
            with mock.patch.object(HELPER_STABILIZATION, "run_shell_command") as run_shell_command:
                HELPER_STABILIZATION.run_validation_commands(["python3 -m unittest", "git diff --check"])

        self.assertEqual(
            [call.args[0] for call in run_shell_command.call_args_list],
            ["python3 -m unittest", "git diff --check"],
        )
        blocked_keys = set(env) - {"PATH", "GITHUB_WORKSPACE"}
        for call in run_shell_command.call_args_list:
            command_env = call.kwargs["env"]
            self.assertEqual(command_env["PATH"], "/usr/bin")
            self.assertEqual(command_env["GITHUB_WORKSPACE"], "/work")
            for key in blocked_keys:
                self.assertNotIn(key, command_env)

    def test_stabilization_prompt_is_loaded_from_checked_in_template(self):
        source = HELPER_STABILIZATION_SCRIPT.read_text(encoding="utf-8")

        self.assertTrue(STABILIZE_GOAL_TEMPLATE.is_file())
        self.assertIn('render_markdown_template(\n        "stabilize-goal.md.in"', source)
        self.assertNotIn("Goal: address the latest standard Agent Review findings", source)
        self.assertIn(
            "Goal: address the latest standard Agent Review findings",
            STABILIZE_GOAL_TEMPLATE.read_text(encoding="utf-8"),
        )

    def test_marker_based_pr_rendering_is_profile_driven(self):
        profile = HELPER_RUNTIME.load_profile(str(PROFILE_FILE))
        rendered = HELPER_REPAIR.render_pr_body_from_template(
            template_text=textwrap.dedent(
                f"""
                # Pull Request

                {HELPER_RUNTIME.PR_AUTOMATION_START}
                old automation text
                {HELPER_RUNTIME.PR_AUTOMATION_END}

                ## Description

                {HELPER_RUNTIME.PR_DESCRIPTION_START}
                old description
                {HELPER_RUNTIME.PR_DESCRIPTION_END}

                - [ ] I have tested these changes locally.
                """
            ).strip(),
            description="Generated description",
            repair_branch=REPAIR_BRANCH,
            automation_name=profile["automation_name"],
        )

        body, title, subject, notes = HELPER_REPAIR.render_repair_metadata_values(
            profile=profile,
            source_run_id="12345",
            source_pr_number="169",
            source_run_url="https://github.com/Arm-Debug/amp-dev-forge/actions/runs/12345",
            source_workflow_name="Perception Experience Kit CI Pipeline",
            repair_branch=REPAIR_BRANCH,
            target_branch="main",
            ticket_id="EXPKITS-4242",
        )

        self.assertIn("Automation actor: `workflow-action-update-agent` bot run using `EXPKITS_AGENT_TOKEN`.", rendered)
        self.assertIn(
            HELPER_REPAIR.render_repair_ci_badge(REPAIR_BRANCH),
            rendered,
        )
        self.assertIn(REPAIR_BRANCH, rendered)
        self.assertIn("Generated description", rendered)
        self.assertIn("- [ ] I have tested these changes locally.", rendered)
        self.assertIn("run-pek-ci", body)
        self.assertIn("Source PR: #169", body)
        self.assertIn("Authorization label: `agent-autorepair`", body)
        self.assertIn("Definition of Done", body)
        self.assertIn("The repair branch is based on `main`", body)
        self.assertIn(HELPER_RUNTIME.PR_AUTOMATION_START, body)
        self.assertIn(HELPER_RUNTIME.PR_DESCRIPTION_START, body)
        self.assertEqual(
            HELPER_REPAIR.render_repair_ci_badge(REPAIR_BRANCH),
            "[![Perception Experience Kit CI Pipeline]"
            "(https://github.com/Arm-Debug/amp-dev-forge/actions/workflows/pek-ci.yml/badge.svg"
            f"?branch={REPAIR_BRANCH})]"
            "(https://github.com/Arm-Debug/amp-dev-forge/actions/workflows/pek-ci.yml)",
        )
        self.assertEqual(title.strip(), "[bot] Repair workflow failures from run 12345")
        self.assertEqual(subject.strip(), "[bot] Repair workflow failures from run 12345")
        self.assertIn("Source workflow: Perception Experience Kit CI Pipeline", notes)

    def test_apply_repair_changes_passes_target_branch_to_metadata_renderer(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = Path(temp_dir)
            patch_root = temp_root / "artifact"
            patch_root.mkdir()
            (patch_root / "workflow-action-update-agent.patch").write_text(
                "diff --git a/README.md b/README.md\n",
                encoding="utf-8",
            )
            output_file = temp_root / "outputs.txt"
            body_file = temp_root / "body.md"
            title_file = temp_root / "title.txt"
            subject_file = temp_root / "subject.txt"
            notes_file = temp_root / "notes.txt"
            subject_file.write_text("[bot] Repair workflow failures from run 12345\n", encoding="utf-8")
            notes_file.write_text("Source workflow: CI\n", encoding="utf-8")
            args = argparse.Namespace(
                profile_path=str(PROFILE_FILE),
                patch_root=str(patch_root),
                repair_branch=REPAIR_BRANCH,
                target_branch="main",
                source_run_id="12345",
                source_pr_number="169",
                source_run_url="https://github.com/Arm-Debug/amp-dev-forge/actions/runs/12345",
                source_workflow_name="CI",
                ticket_id="EXPKITS-4242",
                body_file=str(body_file),
                pr_title_file=str(title_file),
                commit_subject_file=str(subject_file),
                commit_notes_file=str(notes_file),
                github_output=str(output_file),
            )

            def fake_run_command(command, *, capture_output=False, check=True, env=None):  # noqa: ANN001
                del capture_output, check, env
                stdout = "feedface\n" if command == ["git", "rev-parse", "HEAD"] else ""
                return subprocess.CompletedProcess(command, 0, stdout=stdout, stderr="")

            with mock.patch.object(HELPER_REPAIR, "run_command", side_effect=fake_run_command):
                with mock.patch.object(HELPER_REPAIR, "write_repair_metadata_files") as write_metadata:
                    result = HELPER_REPAIR.command_apply_repair_changes_and_push(args)

            self.assertEqual(result, 0)
            self.assertEqual(write_metadata.call_args.kwargs["target_branch"], "main")
            outputs = dict(
                line.split("=", 1)
                for line in output_file.read_text(encoding="utf-8").splitlines()
                if line.strip()
            )
            self.assertEqual(outputs["head_sha"], "feedface")

    def test_agent_review_publish_and_fetch_scripts_share_structured_state(self):
        review: dict[str, Any] = {
            "summary": "Looks fine with one minor note.",
            "overall_recommendation": "comment",
            "overall_score": 0.3,
            "overall_confidence": 0.9,
            "findings": [
                {
                    "title": "Minor note",
                    "severity": "note",
                    "score": 0.2,
                    "confidence": 0.8,
                    "path": ".github/workflows/example.yml",
                    "diff_side": "RIGHT",
                    "start_line": 12,
                    "end_line": 12,
                    "body": "Nit: keep names aligned.",
                    "suggestion": "name: Example",
                }
            ],
        }

        markdown = AGENT_REVIEW_MARKDOWN.format_markdown(
            review,
            run_id="28000000001",
            head_sha="deadbeef",
        )
        inline_comment = AGENT_REVIEW_COMMENTS.build_inline_comment_body(
            review["findings"][0],
            run_id="28000000001",
        )

        self.assertIn(OPENAI_AGENT_CONTRACTS.MARKER, markdown)
        self.assertIn(OPENAI_AGENT_CONTRACTS.STATE_MARKER, markdown)
        self.assertIn('"finding_count":1', markdown)
        self.assertNotIn('"findings":[]', markdown)
        self.assertIn("### Findings", markdown)
        self.assertIn("**Minor note**", markdown)
        self.assertIn("Location: `.github/workflows/example.yml:L12 (RIGHT)`", markdown)
        self.assertIn("Nit: keep names aligned.", markdown)
        self.assertIn(OPENAI_AGENT_CONTRACTS.INLINE_MARKER, inline_comment)
        self.assertIn(OPENAI_AGENT_CONTRACTS.INLINE_STATE_MARKER, inline_comment)
        self.assertEqual(AGENT_REVIEW_STATE.EMPTY_REVIEW_STATE["overall_recommendation"], "")
        self.assertEqual(OPENAI_AGENT_CONTRACTS.MARKER, AGENT_REVIEW_FETCH.MARKER)
        self.assertEqual(OPENAI_AGENT_CONTRACTS.STATE_MARKER, AGENT_REVIEW_FETCH.STATE_MARKER)
        self.assertEqual(OPENAI_AGENT_CONTRACTS.INLINE_MARKER, AGENT_REVIEW_FETCH.INLINE_MARKER)
        self.assertEqual(
            OPENAI_AGENT_CONTRACTS.INLINE_STATE_MARKER,
            AGENT_REVIEW_FETCH.INLINE_STATE_MARKER,
        )
        self.assertNotIn(
            'MARKER = "<!-- agent-review-comment -->"',
            AGENT_REVIEW_PUBLISH_SCRIPT.read_text(encoding="utf-8"),
        )
        self.assertNotIn(
            'MARKER = "<!-- agent-review-comment -->"',
            AGENT_REVIEW_FETCH_SCRIPT.read_text(encoding="utf-8"),
        )

    def test_agent_review_publish_submits_inline_findings_with_review(self):
        finding = {
            "title": "Blocking note",
            "severity": "major",
            "score": 0.78,
            "confidence": 0.9,
            "path": ".github/workflows/example.yml",
            "diff_side": "RIGHT",
            "start_line": 12,
            "end_line": 14,
            "body": "Keep the blocking finding attached to the submitted review.",
            "suggestion": "name: Example\non: pull_request\njobs: {}",
        }
        calls = []

        def fake_github_api_request(url, token, method="GET", payload=None):
            calls.append(
                {
                    "url": url,
                    "token": token,
                    "method": method,
                    "payload": payload,
                }
            )
            return "{}"

        with mock.patch.object(
            AGENT_REVIEW_GITHUB_PUBLISH,
            "github_api_request",
            fake_github_api_request,
        ):
            AGENT_REVIEW_GITHUB_PUBLISH.create_pull_review(
                "Arm-Debug/amp-dev-forge",
                "175",
                "token",
                "review body",
                "request_changes",
                commit_id="deadbeef",
                findings=[finding],
                run_id="28000000001",
            )

        self.assertEqual(len(calls), 1)
        call = calls[0]
        self.assertEqual(
            call["url"],
            "repos/Arm-Debug/amp-dev-forge/pulls/175/reviews",
        )
        self.assertEqual(call["method"], "POST")
        self.assertEqual(call["token"], "token")
        self.assertEqual(call["payload"]["body"], "review body")
        self.assertEqual(call["payload"]["event"], "REQUEST_CHANGES")
        self.assertEqual(call["payload"]["commit_id"], "deadbeef")
        self.assertEqual(len(call["payload"]["comments"]), 1)
        comment = call["payload"]["comments"][0]
        self.assertEqual(comment["path"], ".github/workflows/example.yml")
        self.assertEqual(comment["line"], 14)
        self.assertEqual(comment["side"], "RIGHT")
        self.assertEqual(comment["start_line"], 12)
        self.assertEqual(comment["start_side"], "RIGHT")
        self.assertIn(OPENAI_AGENT_CONTRACTS.INLINE_MARKER, comment["body"])
        self.assertIn(OPENAI_AGENT_CONTRACTS.INLINE_STATE_MARKER, comment["body"])

    def test_agent_review_publish_collapses_left_ranges_to_single_anchor(self):
        finding = {
            "title": "Deleted line note",
            "severity": "major",
            "score": 0.78,
            "confidence": 0.9,
            "path": ".github/workflows/example.yml",
            "diff_side": "LEFT",
            "start_line": 12,
            "end_line": 14,
            "body": "Anchor deleted-code findings without risking the whole review batch.",
        }

        comment = AGENT_REVIEW_COMMENTS.build_review_comment_payload(
            finding,
            run_id="28000000001",
        )

        self.assertEqual(comment["path"], ".github/workflows/example.yml")
        self.assertEqual(comment["line"], 14)
        self.assertEqual(comment["side"], "LEFT")
        self.assertNotIn("start_line", comment)
        self.assertNotIn("start_side", comment)
        self.assertIn(OPENAI_AGENT_CONTRACTS.INLINE_MARKER, comment["body"])

    def test_agent_review_publish_filters_inline_comments_against_local_diff(self):
        diff_text = textwrap.dedent(
            """\
            diff --git a/src/example.py b/src/example.py
            index 1111111..2222222 100644
            --- a/src/example.py
            +++ b/src/example.py
            @@ -10,3 +10,4 @@
             context
            -old_value = 1
            +new_value = 1
            +extra_value = 2
            """
        )
        diff_anchors = AGENT_REVIEW_DIFF_ANCHORS.parse_diff_comment_anchors(diff_text)
        findings = [
            {
                "title": "Valid right range",
                "severity": "major",
                "score": 0.78,
                "confidence": 0.9,
                "path": "src/example.py",
                "diff_side": "RIGHT",
                "start_line": 11,
                "end_line": 12,
                "body": "Both added lines are present in the diff.",
            },
            {
                "title": "Invalid right range",
                "severity": "major",
                "score": 0.78,
                "confidence": 0.9,
                "path": "src/example.py",
                "diff_side": "RIGHT",
                "start_line": 99,
                "end_line": 99,
                "body": "This stale line is not present in the diff.",
            },
            {
                "title": "Valid left line",
                "severity": "major",
                "score": 0.78,
                "confidence": 0.9,
                "path": "src/example.py",
                "diff_side": "LEFT",
                "start_line": 11,
                "end_line": 11,
                "body": "The deleted line is present in the diff.",
            },
        ]

        comments = AGENT_REVIEW_COMMENTS.build_review_comment_payloads(
            findings,
            run_id="28000000001",
            diff_anchors=diff_anchors,
        )

        self.assertIn(("src/example.py", "RIGHT", 10), diff_anchors)
        self.assertIn(("src/example.py", "LEFT", 11), diff_anchors)
        self.assertEqual([comment["line"] for comment in comments], [12, 11])
        self.assertEqual(comments[0]["side"], "RIGHT")
        self.assertEqual(comments[0]["start_line"], 11)
        self.assertEqual(comments[1]["side"], "LEFT")
        self.assertNotIn("start_line", comments[1])

    def test_agent_review_publish_does_not_fallback_after_diff_validated_anchors(self):
        finding = {
            "title": "Blocking note",
            "severity": "major",
            "score": 0.78,
            "confidence": 0.9,
            "path": ".github/workflows/example.yml",
            "diff_side": "RIGHT",
            "start_line": 12,
            "end_line": 12,
            "body": "A validated anchor should fail loudly if GitHub rejects it.",
        }
        calls = []

        def fake_submit_pull_review(repository, pr_number, token, payload):
            calls.append(payload)
            raise urllib.error.HTTPError(
                "https://api.github.com/repos/Arm-Debug/amp-dev-forge/pulls/175/reviews",
                422,
                "Validation Failed",
                hdrs=http_headers(),
                fp=io.BytesIO(b'{"message":"Validation Failed"}'),
            )

        with mock.patch.object(
            AGENT_REVIEW_GITHUB_PUBLISH,
            "submit_pull_review",
            fake_submit_pull_review,
        ):
            with self.assertRaises(urllib.error.HTTPError):
                AGENT_REVIEW_GITHUB_PUBLISH.create_pull_review(
                    "Arm-Debug/amp-dev-forge",
                    "175",
                    "token",
                    "review body",
                    "request_changes",
                    commit_id="deadbeef",
                    findings=[finding],
                    run_id="28000000001",
                    diff_anchors={(".github/workflows/example.yml", "RIGHT", 12)},
                )

        self.assertEqual(len(calls), 1)
        self.assertIn("comments", calls[0])

    def test_agent_review_publish_keeps_review_when_inline_anchor_is_rejected(self):
        finding = {
            "title": "Blocking note",
            "severity": "major",
            "score": 0.78,
            "confidence": 0.9,
            "path": ".github/workflows/example.yml",
            "diff_side": "RIGHT",
            "start_line": 12,
            "end_line": 12,
            "body": "Keep the blocking review even when an inline anchor is stale.",
        }
        calls = []

        def fake_submit_pull_review(repository, pr_number, token, payload):
            calls.append(
                {
                    "repository": repository,
                    "pr_number": pr_number,
                    "token": token,
                    "payload": payload,
                }
            )
            if len(calls) == 1:
                raise urllib.error.HTTPError(
                    "https://api.github.com/repos/Arm-Debug/amp-dev-forge/pulls/175/reviews",
                    422,
                    "Validation Failed",
                    hdrs=http_headers(),
                    fp=io.BytesIO(b'{"message":"Validation Failed"}'),
                )

        with mock.patch.object(
            AGENT_REVIEW_GITHUB_PUBLISH,
            "submit_pull_review",
            fake_submit_pull_review,
        ), mock.patch.object(AGENT_REVIEW_GITHUB_PUBLISH.sys, "stderr", io.StringIO()):
            AGENT_REVIEW_GITHUB_PUBLISH.create_pull_review(
                "Arm-Debug/amp-dev-forge",
                "175",
                "token",
                "review body",
                "request_changes",
                commit_id="deadbeef",
                findings=[finding],
                run_id="28000000001",
            )

        self.assertEqual(len(calls), 2)
        self.assertIn("comments", calls[0]["payload"])
        self.assertEqual(calls[1]["payload"]["body"], "review body")
        self.assertEqual(calls[1]["payload"]["event"], "REQUEST_CHANGES")
        self.assertEqual(calls[1]["payload"]["commit_id"], "deadbeef")
        self.assertNotIn("comments", calls[1]["payload"])

    def test_agent_review_fetch_reads_state_from_pull_review_bodies(self):
        issue_comment = {
            "body": "unrelated",
            "user": {"login": "github-actions[bot]"},
            "created_at": "2026-06-30T10:00:00Z",
        }
        stale_review = {
            "body": (
                f"{AGENT_REVIEW_FETCH.MARKER}\n"
                f"{AGENT_REVIEW_FETCH.STATE_MARKER}"
                '{"overall_recommendation":"comment","summary":"old","overall_score":0.1,'
                '"overall_confidence":0.2,"findings":[],"run_id":"old"} -->\n'
            ),
            "user": {"login": "github-actions[bot]"},
            "submitted_at": "2026-06-30T10:01:00Z",
        }
        latest_review = {
            "body": (
                f"{AGENT_REVIEW_FETCH.MARKER}\n"
                f"{AGENT_REVIEW_FETCH.STATE_MARKER}"
                '{"overall_recommendation":"approve","summary":"new","overall_score":0.9,'
                '"overall_confidence":0.8,"findings":[],"run_id":"new"} -->\n'
            ),
            "user": {"login": "github-actions[bot]"},
            "submitted_at": "2026-06-30T10:02:00Z",
        }

        comments = AGENT_REVIEW_FETCH.summary_state_comments(
            [issue_comment],
            [latest_review, stale_review],
            {"github-actions[bot]"},
        )
        state = AGENT_REVIEW_FETCH.extract_state_metadata(comments[-1]["body"])

        self.assertEqual(state["summary"], "new")
        self.assertEqual(state["overall_recommendation"], "approve")
        self.assertEqual(state["run_id"], "new")

    def test_agent_review_fetch_preserves_marker_count_when_extra_inline_findings_are_recovered(self):
        expected_finding = {
            "title": "Expected finding",
            "severity": "major",
            "score": 0.7,
            "confidence": 0.9,
            "path": ".github/workflows/example.yml",
            "diff_side": "RIGHT",
            "start_line": 12,
            "end_line": 12,
            "body": "Fix the expected issue.",
        }
        extra_finding = {
            "title": "Extra finding",
            "severity": "note",
            "score": 0.2,
            "confidence": 0.8,
            "path": ".github/workflows/extra.yml",
            "diff_side": "RIGHT",
            "start_line": 4,
            "end_line": 4,
            "body": "This UI comment was not counted by the summary marker.",
        }
        summary_body = AGENT_REVIEW_MARKDOWN.format_markdown(
            {
                "summary": "One counted finding.",
                "overall_recommendation": "request_changes",
                "overall_score": 0.7,
                "overall_confidence": 0.9,
                "findings": [expected_finding],
            },
            run_id="28000000001",
            head_sha="deadbeef",
        )
        pull_comments = [
            {
                "body": AGENT_REVIEW_COMMENTS.build_inline_comment_body(finding, run_id="28000000001"),
                "user": {"login": "github-actions[bot]"},
                "created_at": "2026-06-30T10:03:00Z",
            }
            for finding in (expected_finding, extra_finding)
        ]

        with tempfile.TemporaryDirectory() as temp_dir:
            output_path = Path(temp_dir) / "review-state.json"
            with mock.patch.dict(
                os.environ,
                {
                    "GITHUB_TOKEN": "token",
                    "GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge",
                    "GITHUB_PR_NUMBER": "123",
                },
                clear=False,
            ):
                with mock.patch.object(AGENT_REVIEW_FETCH, "list_issue_comments", return_value=[]):
                    with mock.patch.object(
                        AGENT_REVIEW_FETCH,
                        "list_pull_reviews",
                        return_value=[
                            {
                                "body": summary_body,
                                "user": {"login": "github-actions[bot]"},
                                "submitted_at": "2026-06-30T10:02:00Z",
                            }
                        ],
                    ):
                        with mock.patch.object(AGENT_REVIEW_FETCH, "list_pull_comments", return_value=pull_comments):
                            with mock.patch.object(
                                AGENT_REVIEW_FETCH.sys,
                                "argv",
                                ["fetch.py", "--output", str(output_path)],
                            ):
                                AGENT_REVIEW_FETCH.main()

            state = json.loads(output_path.read_text(encoding="utf-8"))

        self.assertEqual(state["finding_count"], 1)
        self.assertTrue(state["finding_count_available"])
        self.assertEqual(len(state["findings"]), 2)

    def test_agent_review_fetch_accepts_default_github_actions_authors(self):
        with mock.patch.dict(os.environ, {}, clear=True):
            author_logins = AGENT_REVIEW_FETCH.allowed_author_logins()

        self.assertEqual(author_logins, {"github-actions", "github-actions[bot]"})

    def test_workflow_audit_reports_freshness_only(self):
        workflow = load_yaml(WORKFLOW_AUDIT_FILE)
        dispatch_inputs = workflow["on"]["workflow_dispatch"]["inputs"]
        pull_request_paths = workflow["on"]["pull_request"]["paths"]
        report_job = workflow["jobs"]["workflow-dependency-freshness"]
        report_steps = step_map(report_job)

        self.assertEqual(
            set(workflow["jobs"].keys()),
            {"workflow-dependency-freshness"},
        )
        self.assertEqual(
            set(dispatch_inputs.keys()),
            {"summary_limit"},
        )
        self.assertNotIn("outputs", report_job)
        self.assertIn("scripts/private/agent_runtime/github_api.py", pull_request_paths)
        self.assertIn("--summary-limit", report_steps["Render workflow dependency freshness report"]["run"])
        self.assertNotIn("--github-output", report_steps["Render workflow dependency freshness report"]["run"])

    def test_workflow_audit_fetch_latest_ref_uses_shared_github_api_client(self):
        with mock.patch.object(
            WORKFLOW_AUDIT_REPORT,
            "github_api_json_or_empty",
            side_effect=[{}, [{"name": "v6"}]],
        ) as github_api_json_or_empty:
            latest_ref, latest_source = WORKFLOW_AUDIT_REPORT.fetch_latest_ref(
                None,
                "actions/checkout",
            )

        self.assertEqual(latest_ref, "v6")
        self.assertEqual(latest_source, "tag")
        self.assertEqual(
            [call.args[0] for call in github_api_json_or_empty.call_args_list],
            [
                "repos/actions/checkout/releases/latest",
                "repos/actions/checkout/tags?per_page=1",
            ],
        )
        self.assertEqual(
            [call.kwargs["token"] for call in github_api_json_or_empty.call_args_list],
            [None, None],
        )

    def test_wait_for_review_state_returns_observed_recommendation(self):
        with mock.patch.dict(os.environ, {"GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge"}, clear=False):
            with mock.patch.object(
                HELPER_STABILIZATION,
                "read_review_state",
                return_value={
                    "run_id": "28000000001",
                    "head_sha": "deadbeef",
                    "overall_recommendation": "approve",
                    "findings": [],
                },
            ):
                with mock.patch.object(
                    AGENT_REVIEW_STATE,
                    "read_review_artifact_state",
                    return_value={},
                ):
                    review_state = HELPER_STABILIZATION.wait_for_review_state(
                        pr_number="123",
                        workflow_name="Agent Review",
                        review_state_script="scripts/private/agent_runtime/review/fetch.py",
                        expected_run_id="28000000001",
                        head_sha="deadbeef",
                    )
        self.assertEqual(review_state["overall_recommendation"], "approve")

    def test_wait_for_review_state_falls_back_to_review_artifact(self):
        with mock.patch.dict(os.environ, {"GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge"}, clear=False):
            with mock.patch.object(
                HELPER_STABILIZATION,
                "read_review_state",
                return_value={
                    "run_id": "28000000001",
                    "head_sha": "deadbeef",
                    "overall_recommendation": "request_changes",
                    "finding_count": 1,
                    "findings": [],
                },
            ):
                with mock.patch.object(
                    AGENT_REVIEW_STATE,
                    "read_review_artifact_state",
                    return_value={
                        "run_id": "28000000001",
                        "head_sha": "deadbeef",
                        "overall_recommendation": "request_changes",
                        "findings": [
                            {
                                "title": "Blocking finding",
                                "path": ".github/workflows/example.yml",
                                "body": "Fix it.",
                            }
                        ],
                    },
                ):
                    review_state = HELPER_STABILIZATION.wait_for_review_state(
                        pr_number="123",
                        workflow_name="Agent Review",
                        review_state_script="scripts/private/agent_runtime/review/fetch.py",
                        expected_run_id="28000000001",
                        head_sha="deadbeef",
                    )

        self.assertEqual(review_state["overall_recommendation"], "request_changes")
        self.assertEqual(review_state["findings"][0]["title"], "Blocking finding")

    def test_wait_for_workflow_run_completion_polls_actions_api_instead_of_gh_watch(self):
        with mock.patch.object(
            OPENAI_AGENT_GITHUB_ACTIONS,
            "github_api_json",
            side_effect=[
                {"status": "in_progress", "conclusion": None},
                {"status": "completed", "conclusion": "success"},
            ],
        ) as github_api_json:
            with mock.patch.object(OPENAI_AGENT_GITHUB_ACTIONS.time, "sleep") as sleep:
                HELPER_GITHUB_WORKFLOWS.wait_for_workflow_run_completion(
                    repository="Arm-Debug/amp-dev-forge",
                    workflow_name="Agent Review",
                    run_id="28232063832",
                )

        self.assertEqual(github_api_json.call_count, 2)
        sleep.assert_called_once_with(15)

    def test_find_latest_workflow_run_for_head_accepts_manual_review_runs(self):
        with mock.patch.object(
            OPENAI_AGENT_GITHUB_ACTIONS,
            "github_api_json",
            return_value={
                "workflow_runs": [
                    {
                        "id": "28235500001",
                        "event": "workflow_dispatch",
                        "head_sha": "deadbeef",
                        "created_at": "2026-06-26T11:40:00Z",
                    },
                    {
                        "id": "28235400001",
                        "event": "pull_request",
                        "head_sha": "deadbeef",
                        "created_at": "2026-06-26T11:35:00Z",
                    },
                ]
            },
        ):
            run_id = OPENAI_AGENT_GITHUB_ACTIONS.find_latest_workflow_run_for_head(
                repository="Arm-Debug/amp-dev-forge",
                workflow_file="agent-review.yml",
                branch=REPAIR_BRANCH,
                head_sha="deadbeef",
            )

        self.assertEqual(run_id, "28235500001")

    def test_ensure_allowed_review_recommendation_rejects_requested_changes(self):
        with self.assertRaisesRegex(RuntimeError, "request_changes"):
            HELPER_STABILIZATION.ensure_allowed_review_recommendation(
                pr_number="123",
                workflow_name="Agent Review",
                review_state={"overall_recommendation": "request_changes"},
                allowed_review_recommendations=["approve"],
            )

    def test_stabilize_pr_pushes_followup_commit_until_review_approves(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            context_root = Path(temp_dir) / "context"
            args = argparse.Namespace(
                profile_path=str(PROFILE_FILE),
                pr_number="123",
                repair_branch=REPAIR_BRANCH,
                head_sha="deadbeef",
                ticket_id="EXPKITS-4242",
                source_run_id="12345",
                context_root=str(context_root),
                merge_when_stable=True,
            )
            first_review_state = {
                "run_id": "28000000001",
                "head_sha": "deadbeef",
                "overall_recommendation": "comment",
                "summary": "Tighten the PR stabilization loop.",
                "findings": [{"title": "Loop", "path": ".github/workflows/example.yml", "body": "Retry it."}],
            }
            second_review_state = {
                "run_id": "28000000002",
                "head_sha": "feedface",
                "overall_recommendation": "approve",
                "summary": "Looks good.",
                "findings": [],
            }
            run_command_result = mock.Mock(returncode=0, stdout="", stderr="")

            with mock.patch.dict(
                os.environ,
                {"GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge", "GITHUB_REF_NAME": "feature/test"},
                clear=False,
            ):
                with mock.patch.object(HELPER_STABILIZATION, "run_command", return_value=run_command_result):
                    with mock.patch.object(
                        HELPER_STABILIZATION,
                        "ensure_validation_workflow_run",
                        side_effect=[
                            ("review-1", "pull_request"),
                            ("review-2", "workflow_dispatch"),
                            ("pek-2", "workflow_dispatch"),
                            ("sonar-2", "workflow_dispatch"),
                        ],
                    ) as ensure_validation_workflow_run:
                        with mock.patch.object(
                            HELPER_STABILIZATION,
                            "wait_for_review_state",
                            side_effect=[first_review_state, second_review_state],
                        ):
                            with mock.patch.object(
                                HELPER_STABILIZATION,
                                "dispatch_stabilizer_workflow",
                                return_value="stabilize-1",
                            ) as dispatch_stabilizer_workflow:
                                with mock.patch.object(
                                    HELPER_STABILIZATION,
                                    "read_pr_details",
                                    side_effect=[
                                        {
                                            "repair_branch": REPAIR_BRANCH,
                                            "head_sha": "deadbeef",
                                            "target_branch": "main",
                                        },
                                        {
                                            "repair_branch": REPAIR_BRANCH,
                                            "head_sha": "feedface",
                                            "target_branch": "main",
                                        },
                                    ],
                                ) as read_pr_details:
                                    with mock.patch.object(
                                        HELPER_STABILIZATION,
                                        "publish_review_state_to_pr",
                                    ) as publish_review_state_to_pr:
                                        with mock.patch.object(HELPER_STABILIZATION, "merge_pr") as merge_pr:
                                            result = HELPER_STABILIZATION.command_stabilize_pr(args)

            self.assertEqual(result, 0)
            self.assertEqual(ensure_validation_workflow_run.call_count, 4)
            self.assertEqual(
                [call.kwargs["head_sha"] for call in ensure_validation_workflow_run.call_args_list],
                ["deadbeef", "feedface", "feedface", "feedface"],
            )
            dispatch_stabilizer_workflow.assert_called_once()
            self.assertEqual(read_pr_details.call_args_list, [mock.call("123"), mock.call("123")])
            self.assertEqual(
                publish_review_state_to_pr.call_args.kwargs["review_state"]["run_id"],
                "28000000002",
            )
            publish_review_state_to_pr.assert_called_once_with(
                pr_number="123",
                head_sha="feedface",
                review_state=second_review_state,
            )
            merge_pr.assert_called_once_with("123")

    def test_stabilize_pr_skips_merge_without_merge_flag(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            context_root = Path(temp_dir) / "context"
            args = argparse.Namespace(
                profile_path=str(PROFILE_FILE),
                pr_number="123",
                repair_branch=REPAIR_BRANCH,
                head_sha="feedface",
                ticket_id="EXPKITS-4242",
                source_run_id="12345",
                context_root=str(context_root),
                merge_when_stable=False,
            )
            review_state = {
                "run_id": "28000000002",
                "head_sha": "feedface",
                "overall_recommendation": "approve",
                "summary": "Looks good.",
                "findings": [],
            }
            run_command_result = mock.Mock(returncode=0, stdout="", stderr="")

            with mock.patch.dict(
                os.environ,
                {"GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge", "GITHUB_REF_NAME": "feature/test"},
                clear=False,
            ):
                with mock.patch.object(HELPER_STABILIZATION, "run_command", return_value=run_command_result):
                    with mock.patch.object(
                        HELPER_STABILIZATION,
                        "ensure_validation_workflow_run",
                        side_effect=[
                            ("review-1", "pull_request"),
                            ("pek-1", "pull_request"),
                            ("sonar-1", "pull_request"),
                        ],
                    ) as ensure_validation_workflow_run:
                        with mock.patch.object(HELPER_STABILIZATION, "wait_for_review_state", return_value=review_state):
                            with mock.patch.object(
                                HELPER_STABILIZATION,
                                "read_pr_details",
                                return_value={
                                    "repair_branch": REPAIR_BRANCH,
                                    "head_sha": "feedface",
                                    "target_branch": "main",
                                },
                            ):
                                with mock.patch.object(HELPER_STABILIZATION, "merge_pr") as merge_pr:
                                    result = HELPER_STABILIZATION.command_stabilize_pr(args)

            self.assertEqual(result, 0)
            self.assertEqual(ensure_validation_workflow_run.call_count, 3)
            merge_pr.assert_not_called()

    def test_read_pr_details_returns_same_repository_branch_details(self):
        with mock.patch.dict(os.environ, {"GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge"}, clear=False):
            with mock.patch.object(
                OPENAI_AGENT_GITHUB_ACTIONS,
                "github_api_json",
                return_value={
                    "head": {
                        "ref": REPAIR_BRANCH,
                        "sha": "feedface",
                        "repo": {"full_name": "Arm-Debug/amp-dev-forge"},
                    },
                    "base": {
                        "ref": "main",
                        "repo": {"full_name": "Arm-Debug/amp-dev-forge"},
                    },
                },
            ) as github_api_json:
                details = OPENAI_AGENT_GITHUB_ACTIONS.read_pr_details("175")

        self.assertEqual(github_api_json.call_args.args[0], "repos/Arm-Debug/amp-dev-forge/pulls/175")
        self.assertEqual(details["repair_branch"], REPAIR_BRANCH)
        self.assertEqual(details["head_sha"], "feedface")
        self.assertEqual(details["target_branch"], "main")
        self.assertEqual(details["head_repository"], "Arm-Debug/amp-dev-forge")
        self.assertEqual(details["base_repository"], "Arm-Debug/amp-dev-forge")

    def test_read_pr_details_rejects_fork_pull_request_stabilization(self):
        with mock.patch.dict(os.environ, {"GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge"}, clear=False):
            with mock.patch.object(
                OPENAI_AGENT_GITHUB_ACTIONS,
                "github_api_json",
                return_value={
                    "head": {
                        "ref": "feature/fork-branch",
                        "sha": "feedface",
                        "repo": {"full_name": "contributor/amp-dev-forge"},
                    },
                    "base": {
                        "ref": "main",
                        "repo": {"full_name": "Arm-Debug/amp-dev-forge"},
                    },
                },
            ):
                with self.assertRaisesRegex(RuntimeError, "only supports same-repository pull requests"):
                    OPENAI_AGENT_GITHUB_ACTIONS.read_pr_details("175")

    def test_prepare_stabilization_context_writes_prompt_and_outputs(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            context_root = Path(temp_dir) / "context"
            output_file = Path(temp_dir) / "outputs.txt"
            args = argparse.Namespace(
                profile_path=str(PROFILE_FILE),
                pr_number="123",
                head_sha="deadbeef",
                source_run_id="12345",
                context_root=str(context_root),
                github_output=str(output_file),
            )
            review_state = {
                "run_id": "28000000001",
                "head_sha": "deadbeef",
                "overall_recommendation": "comment",
                "summary": "Tighten the loop.",
                "finding_count": 1,
                "findings": [
                    {
                        "title": "Loop",
                        "path": ".github/workflows/example.yml",
                        "body": "Tighten the loop.",
                    }
                ],
            }

            with mock.patch.object(
                HELPER_STABILIZATION,
                "read_pr_details",
                return_value={
                    "repair_branch": REPAIR_BRANCH,
                    "head_sha": "deadbeef",
                    "target_branch": "main",
                },
            ):
                with mock.patch.object(HELPER_STABILIZATION, "read_review_state", return_value=review_state):
                    with mock.patch.dict(os.environ, {"GITHUB_REPOSITORY": ""}, clear=False):
                        result = HELPER_STABILIZATION.command_prepare_stabilization_context(args)

            self.assertEqual(result, 0)
            outputs = dict(
                line.split("=", 1)
                for line in output_file.read_text(encoding="utf-8").splitlines()
                if line.strip()
            )
            self.assertEqual(outputs["repair_branch"], REPAIR_BRANCH)
            self.assertEqual(outputs["head_sha"], "deadbeef")
            self.assertEqual(outputs["review_recommendation"], "comment")
            self.assertEqual(outputs["agent_model"], "gpt-5.5")
            self.assertTrue((context_root / "review-state.json").is_file())
            self.assertTrue((context_root / "stabilize-goal.md").is_file())

    def test_prepare_stabilization_context_falls_back_to_review_artifact(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            context_root = Path(temp_dir) / "context"
            output_file = Path(temp_dir) / "outputs.txt"
            args = argparse.Namespace(
                profile_path=str(PROFILE_FILE),
                pr_number="123",
                head_sha="deadbeef",
                source_run_id="12345",
                context_root=str(context_root),
                github_output=str(output_file),
            )

            with mock.patch.dict(os.environ, {"GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge"}, clear=False):
                with mock.patch.object(
                    HELPER_STABILIZATION,
                    "read_pr_details",
                    return_value={
                        "repair_branch": REPAIR_BRANCH,
                        "head_sha": "deadbeef",
                        "target_branch": "main",
                    },
                ):
                    with mock.patch.object(
                        HELPER_STABILIZATION,
                        "read_review_state",
                        return_value={
                            "run_id": "",
                            "head_sha": "",
                            "overall_recommendation": "",
                        },
                    ):
                        with mock.patch.object(
                            HELPER_STABILIZATION,
                            "find_latest_workflow_run_for_head",
                            return_value="28000000001",
                        ):
                            with mock.patch.object(
                                AGENT_REVIEW_STATE,
                                "read_review_artifact_state",
                                return_value={
                                    "run_id": "28000000001",
                                    "head_sha": "deadbeef",
                                    "overall_recommendation": "request_changes",
                                    "summary": "Fallback summary",
                                    "findings": [
                                        {
                                            "title": "Fallback finding",
                                            "path": ".github/workflows/example.yml",
                                            "body": "Use canonical artifact details.",
                                        }
                                    ],
                                },
                            ):
                                result = HELPER_STABILIZATION.command_prepare_stabilization_context(args)

            self.assertEqual(result, 0)
            outputs = dict(
                line.split("=", 1)
                for line in output_file.read_text(encoding="utf-8").splitlines()
                if line.strip()
            )
            self.assertEqual(outputs["review_recommendation"], "request_changes")

    def test_prepare_stabilization_context_rejects_summary_only_non_approve_state(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            context_root = Path(temp_dir) / "context"
            output_file = Path(temp_dir) / "outputs.txt"
            args = argparse.Namespace(
                profile_path=str(PROFILE_FILE),
                pr_number="123",
                head_sha="deadbeef",
                source_run_id="12345",
                context_root=str(context_root),
                github_output=str(output_file),
            )
            review_state = {
                "run_id": "28000000001",
                "head_sha": "deadbeef",
                "overall_recommendation": "request_changes",
                "summary": "Fix the workflow.",
                "finding_count": 1,
                "findings": [],
            }

            with mock.patch.object(
                HELPER_STABILIZATION,
                "read_pr_details",
                return_value={
                    "repair_branch": REPAIR_BRANCH,
                    "head_sha": "deadbeef",
                    "target_branch": "main",
                },
            ):
                with mock.patch.object(HELPER_STABILIZATION, "read_review_state", return_value=review_state):
                    with mock.patch.dict(os.environ, {"GITHUB_REPOSITORY": ""}, clear=False):
                        with self.assertRaisesRegex(RuntimeError, "complete actionable findings"):
                            HELPER_STABILIZATION.command_prepare_stabilization_context(args)

    def test_prepare_stabilization_context_rejects_partial_fallback_findings(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            context_root = Path(temp_dir) / "context"
            output_file = Path(temp_dir) / "outputs.txt"
            args = argparse.Namespace(
                profile_path=str(PROFILE_FILE),
                pr_number="123",
                head_sha="deadbeef",
                source_run_id="12345",
                context_root=str(context_root),
                github_output=str(output_file),
            )
            review_state = {
                "run_id": "28000000001",
                "head_sha": "deadbeef",
                "overall_recommendation": "request_changes",
                "summary": "Fix both workflow issues.",
                "finding_count": 2,
                "findings": [
                    {
                        "title": "First recovered finding",
                        "path": ".github/workflows/example.yml",
                        "body": "Only one inline comment was recovered.",
                    }
                ],
            }

            with mock.patch.object(
                HELPER_STABILIZATION,
                "read_pr_details",
                return_value={
                    "repair_branch": REPAIR_BRANCH,
                    "head_sha": "deadbeef",
                    "target_branch": "main",
                },
            ):
                with mock.patch.object(HELPER_STABILIZATION, "read_review_state", return_value=review_state):
                    with mock.patch.dict(os.environ, {"GITHUB_REPOSITORY": ""}, clear=False):
                        with self.assertRaisesRegex(RuntimeError, "recovered_findings=1"):
                            HELPER_STABILIZATION.command_prepare_stabilization_context(args)

    def test_review_state_fallback_requires_positive_complete_finding_count(self):
        fallback_state = AGENT_REVIEW_STATE.normalize_review_state(
            {
                "run_id": "28000000001",
                "head_sha": "deadbeef",
                "overall_recommendation": "request_changes",
                "finding_count": 0,
                "findings": [
                    {
                        "title": "Unexpected inline finding",
                        "path": ".github/workflows/example.yml",
                        "body": "A non-approve fallback with count zero is inconsistent.",
                    }
                ],
            }
        )

        self.assertFalse(
            AGENT_REVIEW_STATE.review_state_can_drive_stabilization(
                fallback_state,
                source="pull request state",
            )
        )
        self.assertTrue(
            AGENT_REVIEW_STATE.review_state_can_drive_stabilization(
                fallback_state,
                source="artifact",
            )
        )

    def test_review_state_fallback_requires_exact_marker_count(self):
        fallback_state = AGENT_REVIEW_STATE.normalize_review_state(
            {
                "run_id": "28000000001",
                "head_sha": "deadbeef",
                "overall_recommendation": "request_changes",
                "finding_count": 1,
                "findings": [
                    {
                        "title": "Expected finding",
                        "path": ".github/workflows/example.yml",
                        "body": "The counted finding.",
                    },
                    {
                        "title": "Unexpected extra finding",
                        "path": ".github/workflows/extra.yml",
                        "body": "Extra UI state must not become canonical.",
                    },
                ],
            }
        )

        self.assertFalse(
            AGENT_REVIEW_STATE.review_state_can_drive_stabilization(
                fallback_state,
                source="pull request state",
            )
        )

    def test_review_state_fallback_does_not_trust_availability_without_valid_count(self):
        fallback_state = AGENT_REVIEW_STATE.normalize_review_state(
            {
                "run_id": "28000000001",
                "head_sha": "deadbeef",
                "overall_recommendation": "request_changes",
                "finding_count_available": True,
                "findings": [
                    {
                        "title": "Recovered finding",
                        "path": ".github/workflows/example.yml",
                        "body": "A count availability flag without a count is not enough.",
                    }
                ],
            }
        )

        self.assertFalse(fallback_state["finding_count_available"])
        self.assertFalse(
            AGENT_REVIEW_STATE.review_state_can_drive_stabilization(
                fallback_state,
                source="pull request state",
            )
        )

    def test_commit_review_fix_uses_pat_remote_and_bot_identity(self):
        review_state = {"run_id": "28000000001", "summary": "Fix the findings."}
        run_command_result = mock.Mock(returncode=1, stdout="", stderr="")

        with mock.patch.dict(
            os.environ,
            {
                "GH_TOKEN": "pat-token",
                "GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge",
                "GITHUB_SERVER_URL": "https://github.com",
            },
            clear=False,
        ):
            with mock.patch.object(
                    HELPER_STABILIZATION,
                "run_command",
                side_effect=[
                    mock.Mock(returncode=0, stdout="", stderr=""),
                    mock.Mock(returncode=0, stdout="", stderr=""),
                    mock.Mock(returncode=0, stdout="", stderr=""),
                    mock.Mock(returncode=0, stdout="", stderr=""),
                    run_command_result,
                    mock.Mock(returncode=0, stdout="", stderr=""),
                    mock.Mock(returncode=0, stdout="", stderr=""),
                    mock.Mock(returncode=0, stdout="feedface\n", stderr=""),
                ],
            ) as run_command:
                with mock.patch.object(HELPER_STABILIZATION, "github_api_json", return_value={"login": "pat-user"}):
                    head_sha = HELPER_STABILIZATION.commit_review_fix(
                        pr_number="169",
                        repair_branch=REPAIR_BRANCH,
                        ticket_id="EXPKITS-1234",
                        review_state=review_state,
                    )

        self.assertEqual(head_sha, "feedface")
        self.assertEqual(run_command.call_args_list[0].args[0], ["git", "config", "user.name", "github-actions[bot]"])
        self.assertEqual(
            run_command.call_args_list[1].args[0],
            ["git", "config", "user.email", "41898282+github-actions[bot]@users.noreply.github.com"],
        )
        remote_command = run_command.call_args_list[2].args[0]
        self.assertEqual(remote_command[:4], ["git", "remote", "set-url", "origin"])
        remote_url = urllib.parse.urlsplit(remote_command[4])
        self.assertEqual(remote_url.scheme, "https")
        credentials, separator, host = remote_url.netloc.rpartition("@")
        self.assertEqual(separator, "@")
        username, separator, token = credentials.partition(":")
        self.assertEqual(username, "pat-user")
        self.assertEqual(separator, ":")
        self.assertTrue(token)
        self.assertEqual(host, "github.com")
        self.assertEqual(remote_url.path, "/Arm-Debug/amp-dev-forge.git")

    def test_publish_review_state_to_pr_reuses_publish_script(self):
        review_state = {
            "run_id": "28000000001",
            "summary": "Looks good.",
            "overall_recommendation": "approve",
            "overall_score": 0.1,
            "overall_confidence": 0.9,
            "findings": [],
        }

        with mock.patch.dict(
            os.environ,
            {
                "GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge",
                "GH_TOKEN": "pat-token",
            },
            clear=False,
        ):
            with mock.patch.object(HELPER_STABILIZATION, "run_command") as run_command:
                HELPER_STABILIZATION.publish_review_state_to_pr(
                    pr_number="169",
                    head_sha="deadbeef",
                    review_state=review_state,
                )

        self.assertEqual(run_command.call_args.args[0][0:2], ["python3", str(AGENT_REVIEW_PUBLISH_SCRIPT)])
        self.assertEqual(run_command.call_args.kwargs["env"]["GITHUB_PR_NUMBER"], "169")
        self.assertEqual(run_command.call_args.kwargs["env"]["GITHUB_HEAD_SHA"], "deadbeef")
        self.assertEqual(run_command.call_args.kwargs["env"]["GITHUB_RUN_ID"], "28000000001")


if __name__ == "__main__":
    unittest.main()
