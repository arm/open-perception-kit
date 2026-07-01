################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
from email.message import Message
import io
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
AGENT_REVIEW_FETCH_SCRIPT = REPO_ROOT / ".github/agent-runtime/review/scripts/fetch-review-state.py"
AGENT_REVIEW_PUBLISH_SCRIPT = REPO_ROOT / ".github/agent-runtime/review/scripts/publish-review.py"
AGENT_REVIEW_RUN_SCRIPT = REPO_ROOT / ".github/agent-runtime/review/scripts/run-review.sh"
AGENT_REVIEW_PROMPT_TEMPLATE = REPO_ROOT / ".github/agent-runtime/review/prompts/review.md.in"
AGENT_REQUIREMENTS_FILE = REPO_ROOT / ".github/agent-runtime/runtime/requirements-openai-agents.txt"
AGENT_STATIC_REQUIREMENTS_FILE = REPO_ROOT / ".github/agent-runtime/runtime/requirements-static-analysis.txt"
AGENT_MYPY_CONFIG_FILE = REPO_ROOT / ".github/agent-runtime/runtime/mypy.ini"
AGENT_MODEL_CONFIG_FILE = REPO_ROOT / ".github/agent-runtime/runtime/agent-models.json"
AGENT_TASK_CONFIG_FILE = REPO_ROOT / ".github/agent-runtime/runtime/agent-tasks.json"
OPENAI_AGENT_RUNNER_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/openai_agent_runner.py"
OPENAI_AGENT_INIT_FILE = REPO_ROOT / "scripts/private/agent_runtime/__init__.py"
OPENAI_AGENT_TASKS_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/agent_tasks.py"
OPENAI_AGENT_CONTRACTS_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/contracts.py"
OPENAI_AGENT_REPO_TOOLS_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/repo_tools.py"
OPENAI_AGENT_SDK_RUNTIME_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/sdk_runtime.py"
OPENAI_AGENT_TASK_CONFIG_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/task_config.py"
OPENAI_AGENT_TASK_ESTIMATOR_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/task_estimator.py"
OPENAI_AGENT_MODEL_CONFIG_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/model_config.py"
OPENAI_AGENT_REVIEW_OUTPUT_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review_output.py"
OPENAI_AGENT_WORKFLOW_PY_FILES = sorted((REPO_ROOT / "scripts/private/agent_runtime").glob("*.py"))
OPENAI_AGENT_WORKFLOW_POLICY_FILES = [
    path
    for path in OPENAI_AGENT_WORKFLOW_PY_FILES
    if path != OPENAI_AGENT_CONTRACTS_SCRIPT
]
HELPER_SCRIPT = REPO_ROOT / "scripts/private/workflow_action_update_agent.py"
HELPER_ACTION_FILE = REPO_ROOT / ".github/actions/workflow-action-update-agent-helper/action.yml"
WORKFLOW_REPAIR_ROOT = REPO_ROOT / ".github/agent-runtime/repair"
PROMPT_TEMPLATE_ROOT = WORKFLOW_REPAIR_ROOT / "prompts"
PROFILE_ROOT = WORKFLOW_REPAIR_ROOT / "profiles"
GOAL_TEMPLATE = PROMPT_TEMPLATE_ROOT / "repair-goal.md.in"
STABILIZE_GOAL_TEMPLATE = PROMPT_TEMPLATE_ROOT / "stabilize-goal.md.in"
CONTEXT_TEMPLATE = PROMPT_TEMPLATE_ROOT / "context.md"
PONYTAIL_TEMPLATE = PROMPT_TEMPLATE_ROOT / "ponytail-review.md"
CONSTRAINTS_TEMPLATE = PROMPT_TEMPLATE_ROOT / "constraints.md"
VALIDATION_TEMPLATE = PROMPT_TEMPLATE_ROOT / "validation.md.in"
PROFILE_FILE = PROFILE_ROOT / "profile.json"
WORKFLOW_AUDIT_PROFILE_FILE = PROFILE_ROOT / "workflow-audit-profile.json"
PULL_REQUEST_TEMPLATE = REPO_ROOT / ".github/PULL_REQUEST_TEMPLATE.md"
REPAIR_BRANCH = "feature/EXPKITS-4242/bot-workflow-action-update-agent-run-12345"  # pragma: allowlist secret
OPENAI_AGENT_RUNNER_LABEL = "self-hosted-ubuntu-latest"
OPENAI_REVIEW_MAX_TURNS = 40
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


def load_agent_workflow_module(path: Path, module_name: str):
    module_path = str(OPENAI_AGENT_RUNNER_SCRIPT.parent.parent)
    sys.path.insert(0, module_path)
    try:
        return load_python_module(path, module_name)
    finally:
        sys.path.remove(module_path)


def load_review_script_module(path: Path, module_name: str):
    module_path = str(AGENT_REVIEW_PUBLISH_SCRIPT.parent)
    sys.path.insert(0, module_path)
    try:
        return load_python_module(path, module_name)
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


HELPER = load_python_module(HELPER_SCRIPT, "workflow_action_update_agent")
WORKFLOW_AUDIT_REPORT = load_python_module(WORKFLOW_AUDIT_REPORT_SCRIPT, "workflow_audit_report")
AGENT_REVIEW_FETCH = load_review_script_module(AGENT_REVIEW_FETCH_SCRIPT, "agent_review_fetch_review_state")
AGENT_REVIEW_PUBLISH = load_review_script_module(AGENT_REVIEW_PUBLISH_SCRIPT, "agent_review_publish_review")
OPENAI_AGENT_CONTRACTS = load_agent_workflow_module(
    OPENAI_AGENT_CONTRACTS_SCRIPT,
    "agent_runtime.contracts",
)
OPENAI_AGENT_MODEL_CONFIG = load_agent_workflow_module(
    OPENAI_AGENT_MODEL_CONFIG_SCRIPT,
    "agent_runtime.model_config",
)
OPENAI_AGENT_TASK_CONFIG = load_agent_workflow_module(
    OPENAI_AGENT_TASK_CONFIG_SCRIPT,
    "agent_runtime.task_config",
)
AGENT_REVIEW_OUTPUT = load_agent_workflow_module(
    OPENAI_AGENT_REVIEW_OUTPUT_SCRIPT,
    "agent_runtime.review_output",
)


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
            {"source_run_id", "target_branch", "ticket_id", "profile_path", "source_artifact_name"},
        )
        self.assertEqual(
            inputs["profile_path"]["default"],
            ".github/agent-runtime/repair/profiles/profile.json",
        )
        self.assertEqual(inputs["source_artifact_name"]["default"], "")

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
                "package-patch",
                "require-generated-patch",
                "apply-patch-and-push",
                "create-draft-pr",
                "stabilize-pr",
            },
        )
        self.assertEqual(agent_job["runs-on"], OPENAI_AGENT_RUNNER_LABEL)
        self.assertEqual(stabilize_job["runs-on"], "ubuntu-latest")
        self.assertIn("Download source artifact context", agent_steps)
        self.assertIn("Install OpenAI agent runtime", agent_steps)
        self.assertIn("Run OpenAI SDK repair agent", agent_steps)
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
            ".agent-runtime/openai-agent-venv/bin/python -m mypy --config-file .github/agent-runtime/runtime/mypy.ini",
            static_regression_step["run"],
        )
        self.assertEqual(
            agent_steps["Download source artifact context"]["if"],
            "${{ inputs.source_artifact_name != '' }}",
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

    def test_helper_action_exposes_structured_outputs(self):
        action = load_yaml(HELPER_ACTION_FILE)

        self.assertEqual(action["runs"]["using"], "composite")
        self.assertEqual(
            action["inputs"]["profile-path"]["default"],
            ".github/agent-runtime/repair/profiles/profile.json",
        )
        self.assertIn("command", action["inputs"])
        self.assertIn("should_run", action["outputs"])
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
                    HELPER_SCRIPT,
                    "workflow_action_update_agent_workspace_root",
                )

        self.assertEqual(workspace_helper.REPO_ROOT, Path(temp_dir).resolve())
        self.assertEqual(
            workspace_helper.resolve_repo_path(".github/agent-runtime/repair/profiles/profile.json"),
            Path(temp_dir).resolve() / ".github/agent-runtime/repair/profiles/profile.json",
        )
        self.assertEqual(
            workspace_helper.profile_config_root(
                ".workflow-action-update-agent-helper/.github/agent-runtime/repair/profiles/profile.json",
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
        redirect_response = mock.MagicMock()
        redirect_response.__enter__.return_value = redirect_response
        redirect_response.read.return_value = b"zip-bytes"

        with mock.patch.dict(os.environ, {"GH_TOKEN": "test-token"}, clear=False):
            with mock.patch("urllib.request.build_opener", return_value=opener):
                with mock.patch("urllib.request.urlopen", return_value=redirect_response) as urlopen:
                    result = HELPER.download_github_archive(
                        "https://api.github.com/repos/Arm-Debug/amp-dev-forge/actions/artifacts/1/zip",
                    )

        self.assertEqual(result, b"zip-bytes")
        self.assertEqual(urlopen.call_args.args[0], "https://objects.githubusercontent.com/archive.zip")

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
                    HELPER,
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
                        HELPER,
                        "download_github_archive",
                        side_effect=[log_archive, artifact_archive],
                    ):
                        result = HELPER.command_collect_context(args)
            self.assertEqual(result, 0)
            self.assertTrue((context_root / "source-run.json").is_file())
            self.assertIn("hello from logs", (context_root / "source-run.log").read_text(encoding="utf-8"))
            self.assertTrue(
                (context_root / "artifacts/workflow-dependency-freshness/report.md").is_file()
            )

    def test_agent_review_workflow_uses_openai_sdk_proxy_flow(self):
        workflow = load_yaml(AGENT_REVIEW_WORKFLOW_FILE)
        dispatch_inputs = workflow["on"]["workflow_dispatch"]["inputs"]
        review_job = workflow["jobs"]["review"]
        review_steps = step_map(review_job)

        self.assertEqual(review_job["runs-on"], OPENAI_AGENT_RUNNER_LABEL)
        self.assertIn("base_ref", dispatch_inputs)
        self.assertIn("head_ref", dispatch_inputs)
        self.assertEqual(
            list(review_steps),
            [
                "Checkout pull request head",
                "Fetch Agent review base ref",
                "Render Agent review prompt",
                "Install OpenAI agent runtime",
                "Run Agent workflow static analysis",
                "Run OpenAI SDK review",
                "Render review summary",
                "Publish review summary comment",
                "Upload review artifacts",
            ],
        )
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
            ".agent-runtime/openai-agent-venv/bin/python -m mypy --config-file .github/agent-runtime/runtime/mypy.ini",
            static_step["run"],
        )
        checkout_step = review_steps["Checkout pull request head"]
        self.assertIn("github.event.inputs.head_ref", checkout_step["with"]["ref"])
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

    def test_openai_sdk_runner_replaces_legacy_action_and_cli_paths(self):
        searched_files = [
            AGENT_REVIEW_WORKFLOW_FILE,
            REUSABLE_WORKFLOW_FILE,
            STABILIZER_WORKFLOW_FILE,
            AGENT_REVIEW_RUN_SCRIPT,
        ]

        for path in searched_files:
            content = path.read_text(encoding="utf-8")
            self.assertNotIn("openai/codex-action@v1", content, path)
            self.assertNotIn("codex exec", content, path)
            self.assertNotIn("openai-agent-runtime", content, path)
            self.assertNotIn("openai_agent_runtime.sh", content, path)
        self.assertFalse((REPO_ROOT / ".github/actions/openai-agent-runtime/action.yml").exists())
        self.assertFalse((REPO_ROOT / "scripts/private/agent_runtime/openai_agent_runtime.sh").exists())
        for path in searched_files:
            self.assertIn("openai_agent_runner.py", path.read_text(encoding="utf-8"), path)

    def test_agent_workflow_contracts_do_not_use_legacy_codex_names(self):
        searched_files = [
            WORKFLOW_FILE,
            REUSABLE_WORKFLOW_FILE,
            STABILIZER_WORKFLOW_FILE,
            WORKFLOW_AUDIT_FILE,
            AGENT_REVIEW_WORKFLOW_FILE,
            HELPER_ACTION_FILE,
            HELPER_SCRIPT,
            AGENT_REVIEW_FETCH_SCRIPT,
            AGENT_REVIEW_PUBLISH_SCRIPT,
            AGENT_REVIEW_RUN_SCRIPT,
            CONTEXT_TEMPLATE,
            CONSTRAINTS_TEMPLATE,
            GOAL_TEMPLATE,
            PROFILE_FILE,
            WORKFLOW_AUDIT_PROFILE_FILE,
            AGENT_STATIC_REQUIREMENTS_FILE,
            AGENT_MYPY_CONFIG_FILE,
            AGENT_MODEL_CONFIG_FILE,
            AGENT_TASK_CONFIG_FILE,
            OPENAI_AGENT_INIT_FILE,
            OPENAI_AGENT_MODEL_CONFIG_SCRIPT,
            *OPENAI_AGENT_WORKFLOW_POLICY_FILES,
        ]
        forbidden = [
            "codex-review",
            "codex-stabilize",
            "Codex Review",
            "Codex Stabilize",
            "codex_model",
            "CODEX_REVIEW",
            ".github/ci/workflow-action-update-agent",
            "scripts/private/openai_agent_runner.py",
            ".github/agent-workflows",
            ".agent-workflows",
            "scripts/private/agent_workflows",
            "agent_workflows",
            "workflow-repair",
            "requirements-agent.txt",
            "gpt-5.3-codex",
        ]

        for path in searched_files:
            content = path.read_text(encoding="utf-8")
            for token in forbidden:
                self.assertNotIn(token, content, f"{token} leaked in {path}")

    def test_openai_agent_runner_uses_arm_proxy_truststore_and_tracing_contract(self):
        runner_source = OPENAI_AGENT_RUNNER_SCRIPT.read_text(encoding="utf-8")
        sdk_source = OPENAI_AGENT_SDK_RUNTIME_SCRIPT.read_text(encoding="utf-8")
        task_source = OPENAI_AGENT_TASKS_SCRIPT.read_text(encoding="utf-8")
        tools_source = OPENAI_AGENT_REPO_TOOLS_SCRIPT.read_text(encoding="utf-8")
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
        truststore_import = sdk_source.index("import truststore")
        inject_call = sdk_source.index("truststore.inject_into_ssl()")
        agents_import = sdk_source.index("from agents import")
        self.assertLess(truststore_import, agents_import)
        self.assertLess(inject_call, agents_import)
        self.assertIn("def configure_openai_environment", sdk_source)
        self.assertIn("class AgentWorkflowTask", task_source)
        self.assertIn("class ReviewAgentTask", task_source)
        self.assertIn("class RepairAgentTask", task_source)
        self.assertIn("class StabilizationAgentTask", task_source)
        self.assertIn("class ReviewResult", task_source)
        self.assertIn("return ReviewResult", task_source)
        self.assertIn("filter_invalid_right_side_findings", task_source)
        self.assertIn("import shlex", tools_source)
        self.assertIn("def split_shell_commands", tools_source)
        self.assertIn("def find_subcommand", tools_source)
        self.assertIn("READ_ONLY_GIT_SUBCOMMANDS", tools_source)
        self.assertIn("FORBIDDEN_GIT_OPTIONS", tools_source)
        self.assertIn("def is_allowed_git_command", tools_source)
        self.assertIn("def has_forbidden_git_option", tools_source)
        self.assertIn('"apply"', tools_source)
        self.assertIn('["git", "apply", "--whitespace=nowarn"]', tools_source)
        self.assertIn("class TaskEstimate", estimator_source)
        self.assertIn("class TaskEstimatorAgent", estimator_source)
        self.assertIn("async def estimate_task_fit", estimator_source)
        self.assertIn("def build_task_manifest", estimator_source)
        self.assertIn("class AgentTaskSettings", task_config_source)
        self.assertIn("def resolve_agent_task_settings", task_config_source)
        self.assertIn("from .contracts import", model_config_source)
        self.assertIn("from .contracts import", task_config_source)
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

    def test_openai_agent_runner_executes_simple_commands_without_shell_expansion(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.repo_tools_fake_sdk_commands",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_tools.set_run_context(Path(temp_dir), 10)
            output = repo_tools.run_shell_command('echo "$(git push)" && git diff --check')

        self.assertIn("$ echo '$(git push)'", output)
        self.assertIn("$(git push)", output)
        self.assertIn("$ git diff --check", output)

    def test_openai_agent_runner_blocks_mutating_git_commands_after_shell_splitting(self):
        repo_tools = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_REPO_TOOLS_SCRIPT,
            "agent_runtime.repo_tools_fake_sdk_git_guards",
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
        ):
            repo_tools.reject_unsafe_shell_command(command)
        with self.assertRaisesRegex(ValueError, "git push"):
            repo_tools.reject_unsafe_shell_command('echo ok && git push')
        with self.assertRaisesRegex(ValueError, "Unsupported shell syntax"):
            repo_tools.reject_unsafe_shell_command("echo ok | git push")
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
            "agent_runtime.agent_tasks_fake_sdk_schema",
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            schema_file = Path(temp_dir) / "review.schema.json"
            schema_file.write_text('{"type":"object"}\n', encoding="utf-8")

            agent_tasks.validate_schema_file(str(schema_file))
            with self.assertRaisesRegex(ValueError, "Review schema file does not exist"):
                agent_tasks.validate_schema_file(str(Path(temp_dir) / "missing.schema.json"))

    def test_openai_agent_runner_uses_type_specific_turn_defaults(self):
        args = argparse.Namespace(max_turns=None, task_config_file=str(AGENT_TASK_CONFIG_FILE))
        self.assertEqual(
            OPENAI_AGENT_TASK_CONFIG.resolve_agent_max_turns(OPENAI_AGENT_CONTRACTS.AgentCommand.REVIEW, args),
            OPENAI_REVIEW_MAX_TURNS,
        )
        self.assertEqual(
            OPENAI_AGENT_TASK_CONFIG.resolve_agent_max_turns(OPENAI_AGENT_CONTRACTS.AgentCommand.REPAIR, args),
            OPENAI_PATCH_MAX_TURNS,
        )
        self.assertEqual(
            OPENAI_AGENT_TASK_CONFIG.resolve_agent_max_turns(
                OPENAI_AGENT_CONTRACTS.AgentCommand.STABILIZATION,
                args,
            ),
            OPENAI_PATCH_MAX_TURNS,
        )

        args.max_turns = 12
        self.assertEqual(
            OPENAI_AGENT_TASK_CONFIG.resolve_agent_max_turns(OPENAI_AGENT_CONTRACTS.AgentCommand.REVIEW, args),
            12,
        )

    def test_openai_agent_runner_blocks_prompt_that_exceeds_task_limit(self):
        estimator = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_TASK_ESTIMATOR_SCRIPT,
            "agent_runtime.task_estimator_fake_sdk_prompt_limit",
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

    def test_openai_agent_runner_blocks_review_diff_that_exceeds_file_limit(self):
        estimator = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_TASK_ESTIMATOR_SCRIPT,
            "agent_runtime.task_estimator_fake_sdk_review_limit",
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
            )
            prompt = f"- Base SHA: `{base_sha}`\n- Head SHA: `{head_sha}`\n"
            manifest = estimator.build_task_manifest(
                OPENAI_AGENT_CONTRACTS.AgentCommand.REVIEW,
                prompt,
                settings,
                "gpt-test",
            )
            reasons = estimator.deterministic_task_limit_violations(manifest)

        self.assertIn("review scope touches 3 files", reasons[0])

    def test_openai_agent_runner_blocks_estimated_turn_overrun(self):
        estimator = load_agent_workflow_module_with_fake_sdk(
            OPENAI_AGENT_TASK_ESTIMATOR_SCRIPT,
            "agent_runtime.task_estimator_fake_sdk_turn_limit",
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

        reasons = estimator.task_estimate_block_reasons(manifest, cast(Any, estimate))

        self.assertEqual(
            reasons,
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

    def test_agent_review_output_drops_stale_contract_findings_not_supported_by_anchor(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            profile_path = repo_root / ".github/agent-runtime/repair/profiles/profile.json"
            profile_path.parent.mkdir(parents=True)
            profile_path.write_text(
                '"review_state_script": ".github/agent-runtime/review/scripts/fetch-review-state.py",\n',
                encoding="utf-8",
            )

            payload = {
                "summary": "Reviewed workflow changes.",
                "overall_recommendation": "request_changes",
                "overall_score": 0.9,
                "overall_confidence": 0.96,
                "findings": [
                    {
                        "title": "Profile still points to codex-review",
                        "severity": "major",
                        "score": 0.9,
                        "confidence": 0.96,
                        "path": ".github/agent-runtime/repair/profiles/profile.json",
                        "diff_side": "RIGHT",
                        "start_line": 1,
                        "end_line": 1,
                        "body": "`review_state_script` is still set to `codex-review/scripts/fetch-review-state.py`.",
                        "suggestion": None,
                    },
                ],
            }

            filtered = AGENT_REVIEW_OUTPUT.filter_invalid_right_side_findings(payload, repo_root)

        self.assertEqual(filtered["overall_recommendation"], "approve")
        self.assertEqual(
            filtered["summary"],
            (
                "No supported findings remain after filtering. "
                "Omitted 1 unsupported RIGHT-side finding whose anchors are not supported by the current checkout."
            ),
        )
        self.assertEqual(filtered["findings"], [])

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

    def test_openai_agent_runtime_dependencies_are_pinned(self):
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
                "types-PyYAML==6.0.12.20250516",
            },
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
        self.assertEqual(task_config["tasks"]["run-review"]["max_review_changed_lines"], 10000)
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
        self.assertEqual(settings.max_review_changed_lines, 10000)

    def test_local_review_runner_uses_shared_sdk_script(self):
        content = AGENT_REVIEW_RUN_SCRIPT.read_text(encoding="utf-8")

        self.assertIn('agent_venv="${AGENT_REVIEW_AGENT_VENV:-.agent-runtime/openai-agent-venv}"', content)
        self.assertIn('export REVIEW_BASE_REF="${base_ref}"', content)
        self.assertIn('export REVIEW_HEAD_REF="${REVIEW_HEAD_REF:-HEAD}"', content)
        self.assertIn('export REVIEW_REPOSITORY="${REVIEW_REPOSITORY:-local-checkout}"', content)
        self.assertIn('python3 -m venv "${agent_venv}"', content)
        self.assertIn(
            '"${agent_venv}/bin/python" -m pip install -r .github/agent-runtime/runtime/requirements-openai-agents.txt',
            content,
        )
        self.assertIn("run-review", content)
        self.assertIn(
            '"${agent_venv}/bin/python" scripts/private/agent_runtime/openai_agent_runner.py "${agent_args[@]}"',
            content,
        )
        self.assertNotIn("--command run-review", content)
        self.assertNotIn("--agent-instance review", content)
        self.assertNotIn("--model-config-file .github/agent-runtime/runtime/agent-models.json", content)
        self.assertNotIn("--task-config-file .github/agent-runtime/runtime/agent-tasks.json", content)
        self.assertIn('if [[ -n "${AGENT_REVIEW_MODEL:-}" ]]; then', content)
        self.assertIn('agent_args+=(--model "${AGENT_REVIEW_MODEL}")', content)
        self.assertNotIn("gpt-5.3-codex", content)
        self.assertNotIn("${AGENT_MODEL", content)
        self.assertNotIn("CODEX_MODEL", content)
        self.assertIn("--schema-file \".github/agent-runtime/review/schemas/review.schema.json\"", content)
        self.assertNotIn("command -v codex", content)
        self.assertNotIn("pip install --user", content)
        self.assertNotIn("OPENAI_API_KEY=", content)

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

        self.assertEqual(
            set(pek_inputs.keys()),
            {"pr_number", "pr_base_ref", "pr_head_ref", "pr_head_sha"},
        )
        self.assertEqual(
            set(sonar_inputs.keys()),
            {"pr_number", "pr_base_ref", "pr_head_ref", "pr_head_sha"},
        )
        self.assertIn("github.event.inputs.pr_head_sha", pek_ci["jobs"]["quality-checks"]["steps"][0]["with"]["ref"])
        self.assertIn("github.event.inputs.pr_head_ref", sonar_steps["Checkout"]["with"]["ref"])
        self.assertIn("github.event.inputs.pr_number", sonar_steps["SonarQube analysis"]["env"]["PR_KEY"])

    def test_stabilizer_workflow_uses_canonical_agent_review_shape(self):
        workflow = load_yaml(STABILIZER_WORKFLOW_FILE)
        call_inputs = workflow["on"]["workflow_call"]["inputs"]
        dispatch_inputs = workflow["on"]["workflow_dispatch"]["inputs"]
        job = workflow["jobs"]["stabilize"]
        steps = step_map(job)

        self.assertEqual(set(call_inputs.keys()), set(dispatch_inputs.keys()))
        self.assertEqual(job["runs-on"], OPENAI_AGENT_RUNNER_LABEL)
        self.assertEqual(
            list(steps),
            [
                "Checkout workflow helpers",
                "Snapshot workflow helper bundle",
                "Resolve PR details",
                "Checkout PR head",
                "Restore workflow helper bundle",
                "Prepare stabilization context",
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
        self.assertNotIn("openai-agent-runtime", snapshot_step["run"])
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
            'cp -R .github/agent-runtime/review/scripts/. "${bundle_root}/.github/agent-runtime/review/scripts"',
            snapshot_step["run"],
        )
        self.assertIn(
            'cp -R .github/agent-runtime/repair/prompts/. "${bundle_root}/.github/agent-runtime/repair/prompts"',
            snapshot_step["run"],
        )
        self.assertIn(
            'cp -R .github/agent-runtime/repair/profiles/. "${bundle_root}/.github/agent-runtime/repair/profiles"',
            snapshot_step["run"],
        )
        self.assertNotIn('cp -R agent-review/. "${bundle_root}/agent-review"', snapshot_step["run"])
        self.assertNotIn(".github/agent-runtime/review/out", snapshot_step["run"])
        self.assertNotIn('cp -R scripts/private/. "${bundle_root}/scripts/private"', snapshot_step["run"])
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
        self.assertEqual(
            steps["Run stabilization validation"]["with"]["command"],
            "run-validation",
        )
        self.assertEqual(
            steps["Run stabilization validation"]["with"]["profile-path"],
            ".workflow-action-update-agent-helper/${{ inputs.profile_path }}",
        )
        self.assertEqual(
            steps["Run stabilization validation"]["uses"],
            "./.workflow-action-update-agent-helper/.github/actions/workflow-action-update-agent-helper",
        )
        self.assertEqual(
            steps["Commit stabilization fix"]["with"]["command"],
            "commit-review-fix",
        )
        skip_step = steps["Write stabilization skip artifact"]
        self.assertEqual(skip_step["if"], "${{ steps.context.outputs.review_recommendation == 'approve' }}")
        self.assertIn('mkdir -p "${{ runner.temp }}"', skip_step["run"])
        self.assertIn("workflow-action-update-agent-stabilize-output.md", skip_step["run"])
        self.assertIn("No stabilization agent run was needed", skip_step["run"])
        self.assertEqual(
            steps["Commit stabilization fix"]["env"]["GH_TOKEN"],
            "${{ secrets.EXPKITS_AGENT_TOKEN }}",
        )
        self.assertEqual(
            steps["Commit stabilization fix"]["uses"],
            "./.workflow-action-update-agent-helper/.github/actions/workflow-action-update-agent-helper",
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
            }

            with mock.patch.object(HELPER, "parse_json_command", return_value=run_payload):
                with mock.patch.dict(os.environ, {"GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge"}, clear=False):
                    result = HELPER.command_resolve_inputs(args)

            self.assertEqual(result, 0)
            outputs = dict(
                line.split("=", 1)
                for line in output_file.read_text(encoding="utf-8").splitlines()
                if line.strip()
            )
            self.assertEqual(outputs["should_run"], "true")
            self.assertEqual(outputs["target_branch"], "feature/example/topic")
            self.assertEqual(
                outputs["repair_branch"],
                REPAIR_BRANCH,
            )
            self.assertEqual(outputs["agent_model"], "gpt-5.5")

    def test_audit_profile_allows_non_failure_source_run_and_configures_validation(self):
        audit_profile = HELPER.load_profile(str(WORKFLOW_AUDIT_PROFILE_FILE))

        self.assertFalse(HELPER.profile_bool(audit_profile, "require_failure_conclusion", True))
        self.assertEqual(
            audit_profile["repair_branch_template"],
            "feature/{ticket_id}/bot-workflow-dependency-freshness-{source_run_id}",
        )
        self.assertEqual(audit_profile["pr_trigger_label"], "run-pek-ci")
        validation_workflows = audit_profile["validation_workflows"]
        self.assertEqual(
            [item["workflow_file"] for item in validation_workflows],
            ["agent-review.yml", "workflow-audit.yml", "pek-ci.yml", "sonar.yml"],
        )

    def test_agent_review_gate_is_profile_driven(self):
        profile = HELPER.load_profile(str(PROFILE_FILE))
        validation_workflows = HELPER.profile_validation_workflows(profile)
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
            ".github/agent-runtime/review/scripts/fetch-review-state.py",
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
        self.assertEqual(HELPER.profile_config_root(str(PROFILE_FILE)), REPO_ROOT)
        self.assertEqual(
            HELPER.profile_agent_model(profile, HELPER.AgentInstance.REPAIR, str(PROFILE_FILE)),
            "gpt-5.5",
        )

    def test_profile_drives_markdown_context_files_and_validation_commands(self):
        profile = HELPER.load_profile(str(PROFILE_FILE))

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
                source_run_url="https://github.com/Arm-Debug/amp-dev-forge/actions/runs/12345",
                source_workflow_name="Perception Experience Kit CI Pipeline",
                target_branch="main",
                repair_branch=REPAIR_BRANCH,
                ticket_id="EXPKITS-4242",
            )

            result = HELPER.command_build_markdown(args)

            self.assertEqual(result, 0)
            goal = (context_root / "goal.md").read_text(encoding="utf-8")
            validation = (context_root / "validation.md").read_text(encoding="utf-8")
            inventory = (context_root / "file-inventory.md").read_text(encoding="utf-8")

            self.assertIn("# Workflow Action Update Agent", goal)
            self.assertTrue(CONTEXT_TEMPLATE.is_file())
            for path in profile["prompt_context_files"]:
                self.assertIn(f"- `{path}`", goal)
            for command in profile["validation_commands"]:
                self.assertIn(f"- `{command}`", validation)
            self.assertIn("- `artifacts/summary.txt`", inventory)

    def test_stabilization_prompt_is_loaded_from_checked_in_template(self):
        source = HELPER_SCRIPT.read_text(encoding="utf-8")

        self.assertTrue(STABILIZE_GOAL_TEMPLATE.is_file())
        self.assertIn('render_markdown_template(\n        "stabilize-goal.md.in"', source)
        self.assertNotIn("Goal: address the latest standard Agent Review findings", source)
        self.assertIn(
            "Goal: address the latest standard Agent Review findings",
            STABILIZE_GOAL_TEMPLATE.read_text(encoding="utf-8"),
        )

    def test_marker_based_pr_rendering_is_profile_driven(self):
        profile = HELPER.load_profile(str(PROFILE_FILE))
        rendered = HELPER.render_pr_body_from_template(
            template_text=textwrap.dedent(
                f"""
                # Pull Request

                {HELPER.PR_AUTOMATION_START}
                old automation text
                {HELPER.PR_AUTOMATION_END}

                ## Description

                {HELPER.PR_DESCRIPTION_START}
                old description
                {HELPER.PR_DESCRIPTION_END}

                - [ ] I have tested these changes locally.
                """
            ).strip(),
            description="Generated description",
            repair_branch=REPAIR_BRANCH,
            automation_name=profile["automation_name"],
        )

        body, title, subject, notes = HELPER.render_repair_metadata_values(
            profile=profile,
            source_run_id="12345",
            source_run_url="https://github.com/Arm-Debug/amp-dev-forge/actions/runs/12345",
            source_workflow_name="Perception Experience Kit CI Pipeline",
            repair_branch=REPAIR_BRANCH,
            target_branch="main",
            ticket_id="EXPKITS-4242",
        )

        self.assertIn("Automation actor: `workflow-action-update-agent` bot run using `EXPKITS_AGENT_TOKEN`.", rendered)
        self.assertIn(
            HELPER.render_repair_ci_badge(REPAIR_BRANCH),
            rendered,
        )
        self.assertIn(REPAIR_BRANCH, rendered)
        self.assertIn("Generated description", rendered)
        self.assertIn("- [ ] I have tested these changes locally.", rendered)
        self.assertIn("run-pek-ci", body)
        self.assertIn(HELPER.PR_AUTOMATION_START, body)
        self.assertIn(HELPER.PR_DESCRIPTION_START, body)
        self.assertEqual(title.strip(), "[bot] Repair workflow failures from run 12345")
        self.assertEqual(subject.strip(), "[bot] Repair workflow failures from run 12345")
        self.assertIn("Source workflow: Perception Experience Kit CI Pipeline", notes)

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

        markdown = AGENT_REVIEW_PUBLISH.format_markdown(
            review,
            run_id="28000000001",
            head_sha="deadbeef",
        )
        inline_comment = AGENT_REVIEW_PUBLISH.build_inline_comment_body(
            review["findings"][0],
            run_id="28000000001",
        )

        self.assertIn(AGENT_REVIEW_PUBLISH.MARKER, markdown)
        self.assertIn(AGENT_REVIEW_PUBLISH.STATE_MARKER, markdown)
        self.assertIn("### Findings", markdown)
        self.assertIn("**Minor note**", markdown)
        self.assertIn("Location: `.github/workflows/example.yml:L12 (RIGHT)`", markdown)
        self.assertIn("Nit: keep names aligned.", markdown)
        self.assertIn(AGENT_REVIEW_PUBLISH.INLINE_MARKER, inline_comment)
        self.assertIn(AGENT_REVIEW_PUBLISH.INLINE_STATE_MARKER, inline_comment)
        self.assertEqual(AGENT_REVIEW_FETCH.EMPTY_STATE["overall_recommendation"], "")
        self.assertEqual(AGENT_REVIEW_PUBLISH.MARKER, AGENT_REVIEW_FETCH.MARKER)
        self.assertEqual(AGENT_REVIEW_PUBLISH.STATE_MARKER, AGENT_REVIEW_FETCH.STATE_MARKER)
        self.assertEqual(AGENT_REVIEW_PUBLISH.INLINE_MARKER, AGENT_REVIEW_FETCH.INLINE_MARKER)
        self.assertEqual(
            AGENT_REVIEW_PUBLISH.INLINE_STATE_MARKER,
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
            AGENT_REVIEW_PUBLISH,
            "github_api_request",
            fake_github_api_request,
        ):
            AGENT_REVIEW_PUBLISH.create_pull_review(
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
            "https://api.github.com/repos/Arm-Debug/amp-dev-forge/pulls/175/reviews",
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
        self.assertIn(AGENT_REVIEW_PUBLISH.INLINE_MARKER, comment["body"])
        self.assertIn(AGENT_REVIEW_PUBLISH.INLINE_STATE_MARKER, comment["body"])

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

        comment = AGENT_REVIEW_PUBLISH.build_review_comment_payload(
            finding,
            run_id="28000000001",
        )

        self.assertEqual(comment["path"], ".github/workflows/example.yml")
        self.assertEqual(comment["line"], 14)
        self.assertEqual(comment["side"], "LEFT")
        self.assertNotIn("start_line", comment)
        self.assertNotIn("start_side", comment)
        self.assertIn(AGENT_REVIEW_PUBLISH.INLINE_MARKER, comment["body"])

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
        diff_anchors = AGENT_REVIEW_PUBLISH.parse_diff_comment_anchors(diff_text)
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

        comments = AGENT_REVIEW_PUBLISH.build_review_comment_payloads(
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
            AGENT_REVIEW_PUBLISH,
            "submit_pull_review",
            fake_submit_pull_review,
        ):
            with self.assertRaises(urllib.error.HTTPError):
                AGENT_REVIEW_PUBLISH.create_pull_review(
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
            AGENT_REVIEW_PUBLISH,
            "submit_pull_review",
            fake_submit_pull_review,
        ), mock.patch.object(AGENT_REVIEW_PUBLISH.sys, "stderr", io.StringIO()):
            AGENT_REVIEW_PUBLISH.create_pull_review(
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

    def test_agent_review_fetch_accepts_default_github_actions_authors(self):
        with mock.patch.dict(os.environ, {}, clear=True):
            author_logins = AGENT_REVIEW_FETCH.allowed_author_logins()

        self.assertEqual(author_logins, {"github-actions", "github-actions[bot]"})

    def test_workflow_audit_merges_report_and_repair_in_one_pipeline(self):
        workflow = load_yaml(WORKFLOW_AUDIT_FILE)
        dispatch_inputs = workflow["on"]["workflow_dispatch"]["inputs"]
        report_job = workflow["jobs"]["workflow-dependency-freshness"]
        repair_job = workflow["jobs"]["repair-workflow-dependency-freshness"]
        stabilize_job = workflow["jobs"]["stabilize-existing-pr"]
        report_steps = step_map(report_job)

        self.assertEqual(
            set(dispatch_inputs.keys()),
            {"ticket_id", "repair_profile_path", "stabilize_pr_number", "stabilize_head_sha"},
        )
        self.assertIn("requires_repair", report_job["outputs"])
        self.assertIn("behind_latest", report_job["outputs"])
        self.assertIn("needs_review", report_job["outputs"])
        self.assertIn("--github-output", report_steps["Render workflow dependency freshness report"]["run"])
        self.assertEqual(
            repair_job["uses"],
            "./.github/workflows/workflow-action-update-agent-reusable.yml",
        )
        self.assertEqual(repair_job["with"]["source_run_id"], "${{ github.run_id }}")
        self.assertEqual(repair_job["with"]["source_artifact_name"], "workflow-dependency-freshness")
        self.assertEqual(
            repair_job["with"]["profile_path"],
            "${{ inputs.repair_profile_path || '.github/agent-runtime/repair/profiles/workflow-audit-profile.json' }}",
        )
        self.assertEqual(repair_job["permissions"]["actions"], "write")
        self.assertIn("github.event.inputs.stabilize_pr_number == ''", report_job["if"])
        self.assertIn("github.event.inputs.stabilize_pr_number == ''", repair_job["if"])
        self.assertIn("needs.workflow-dependency-freshness.outputs.requires_repair == 'true'", repair_job["if"])
        self.assertIn("github.event_name == 'schedule'", repair_job["if"])
        self.assertIn("github.event_name == 'workflow_dispatch'", repair_job["if"])
        self.assertEqual(stabilize_job["uses"], "./.github/workflows/agent-stabilize-pr.yml")
        self.assertEqual(
            stabilize_job["if"],
            "${{ github.event_name == 'workflow_dispatch' && github.event.inputs.stabilize_pr_number != '' }}",
        )
        self.assertEqual(stabilize_job["with"]["pr_number"], "${{ github.event.inputs.stabilize_pr_number }}")
        self.assertEqual(stabilize_job["with"]["head_sha"], "${{ github.event.inputs.stabilize_head_sha || '' }}")

    def test_wait_for_review_state_returns_observed_recommendation(self):
        with mock.patch.dict(os.environ, {"GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge"}, clear=False):
            with mock.patch.object(
                HELPER,
                "read_review_state",
                return_value={
                    "run_id": "28000000001",
                    "head_sha": "deadbeef",
                    "overall_recommendation": "comment",
                },
            ):
                with mock.patch.object(
                    HELPER,
                    "read_review_artifact_state",
                    side_effect=AssertionError("artifact fallback should not run when comment state is fresh"),
                ):
                    review_state = HELPER.wait_for_review_state(
                        pr_number="123",
                        workflow_name="Agent Review",
                        review_state_script=".github/agent-runtime/review/scripts/fetch-review-state.py",
                        expected_run_id="28000000001",
                        head_sha="deadbeef",
                    )
        self.assertEqual(review_state["overall_recommendation"], "comment")

    def test_wait_for_review_state_falls_back_to_review_artifact(self):
        with mock.patch.dict(os.environ, {"GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge"}, clear=False):
            with mock.patch.object(
                HELPER,
                "read_review_state",
                return_value=HELPER.read_json_file(Path("/dev/null")) if False else {
                    "run_id": "",
                    "head_sha": "",
                    "overall_recommendation": "",
                },
            ):
                with mock.patch.object(
                    HELPER,
                    "read_review_artifact_state",
                    return_value={
                        "run_id": "28000000001",
                        "head_sha": "deadbeef",
                        "overall_recommendation": "request_changes",
                    },
                ):
                    review_state = HELPER.wait_for_review_state(
                        pr_number="123",
                        workflow_name="Agent Review",
                        review_state_script=".github/agent-runtime/review/scripts/fetch-review-state.py",
                        expected_run_id="28000000001",
                        head_sha="deadbeef",
                    )

        self.assertEqual(review_state["overall_recommendation"], "request_changes")

    def test_wait_for_workflow_run_completion_polls_actions_api_instead_of_gh_watch(self):
        with mock.patch.object(
            HELPER,
            "parse_json_command",
            side_effect=[
                {"status": "in_progress", "conclusion": None},
                {"status": "completed", "conclusion": "success"},
            ],
        ) as parse_json_command:
            with mock.patch.object(HELPER.time, "sleep") as sleep:
                HELPER.wait_for_workflow_run_completion(
                    repository="Arm-Debug/amp-dev-forge",
                    workflow_name="Agent Review",
                    run_id="28232063832",
                )

        self.assertEqual(parse_json_command.call_count, 2)
        sleep.assert_called_once_with(15)

    def test_find_latest_workflow_run_for_head_accepts_manual_review_runs(self):
        with mock.patch.object(
            HELPER,
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
            run_id = HELPER.find_latest_workflow_run_for_head(
                repository="Arm-Debug/amp-dev-forge",
                workflow_file="agent-review.yml",
                repair_branch=REPAIR_BRANCH,
                head_sha="deadbeef",
            )

        self.assertEqual(run_id, "28235500001")

    def test_ensure_allowed_review_recommendation_rejects_requested_changes(self):
        with self.assertRaisesRegex(RuntimeError, "request_changes"):
            HELPER.ensure_allowed_review_recommendation(
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
                with mock.patch.object(HELPER, "run_command", return_value=run_command_result):
                    with mock.patch.object(
                        HELPER,
                        "ensure_validation_workflow_run",
                        side_effect=[
                            ("review-1", "pull_request"),
                            ("review-2", "workflow_dispatch"),
                            ("pek-2", "workflow_dispatch"),
                            ("sonar-2", "workflow_dispatch"),
                        ],
                    ) as ensure_validation_workflow_run:
                        with mock.patch.object(
                            HELPER,
                            "wait_for_review_state",
                            side_effect=[first_review_state, second_review_state],
                        ):
                            with mock.patch.object(
                                HELPER,
                                "dispatch_stabilizer_workflow",
                                return_value="stabilize-1",
                            ) as dispatch_stabilizer_workflow:
                                with mock.patch.object(
                                    HELPER,
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
                                        HELPER,
                                        "publish_review_state_to_pr",
                                    ) as publish_review_state_to_pr:
                                        with mock.patch.object(HELPER, "merge_pr") as merge_pr:
                                            result = HELPER.command_stabilize_pr(args)

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
                "findings": [],
            }

            with mock.patch.object(
                HELPER,
                "read_pr_details",
                return_value={
                    "repair_branch": REPAIR_BRANCH,
                    "head_sha": "deadbeef",
                    "target_branch": "main",
                },
            ):
                with mock.patch.object(HELPER, "read_review_state", return_value=review_state):
                    result = HELPER.command_prepare_stabilization_context(args)

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
                    HELPER,
                    "read_pr_details",
                    return_value={
                        "repair_branch": REPAIR_BRANCH,
                        "head_sha": "deadbeef",
                        "target_branch": "main",
                    },
                ):
                    with mock.patch.object(
                        HELPER,
                        "read_review_state",
                        return_value={
                            "run_id": "",
                            "head_sha": "",
                            "overall_recommendation": "",
                        },
                    ):
                        with mock.patch.object(
                            HELPER,
                            "find_latest_workflow_run_for_head",
                            return_value="28000000001",
                        ):
                            with mock.patch.object(
                                HELPER,
                                "read_review_artifact_state",
                                return_value={
                                    "run_id": "28000000001",
                                    "head_sha": "deadbeef",
                                    "overall_recommendation": "request_changes",
                                    "summary": "Fallback summary",
                                    "findings": [],
                                },
                            ):
                                result = HELPER.command_prepare_stabilization_context(args)

            self.assertEqual(result, 0)
            outputs = dict(
                line.split("=", 1)
                for line in output_file.read_text(encoding="utf-8").splitlines()
                if line.strip()
            )
            self.assertEqual(outputs["review_recommendation"], "request_changes")

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
                    HELPER,
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
                with mock.patch.object(HELPER, "github_api_json", return_value={"login": "pat-user"}):
                    head_sha = HELPER.commit_review_fix(
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
            with mock.patch.object(HELPER, "run_command") as run_command:
                HELPER.publish_review_state_to_pr(
                    pr_number="169",
                    head_sha="deadbeef",
                    review_state=review_state,
                )

        self.assertEqual(run_command.call_args.args[0][0:2], ["python3", str(AGENT_REVIEW_PUBLISH_SCRIPT)])
        self.assertEqual(run_command.call_args.kwargs["env"]["GITHUB_PR_NUMBER"], "169")
        self.assertEqual(run_command.call_args.kwargs["env"]["GITHUB_HEAD_SHA"], "deadbeef")
        self.assertEqual(run_command.call_args.kwargs["env"]["GITHUB_RUN_ID"], "28000000001")

    def test_workflow_audit_report_writes_repair_outputs(self):
        entries = [
            {"status": "behind"},
            {"status": "different"},
            {"status": "pinned"},
            {"status": "up-to-date"},
        ]

        with tempfile.TemporaryDirectory() as temp_dir:
            output_path = Path(temp_dir) / "github-output.txt"
            WORKFLOW_AUDIT_REPORT.write_github_outputs(output_path, entries)
            outputs = dict(
                line.split("=", 1)
                for line in output_path.read_text(encoding="utf-8").splitlines()
                if line.strip()
            )

        self.assertEqual(outputs["tracked_refs"], "4")
        self.assertEqual(outputs["behind_latest"], "2")
        self.assertEqual(outputs["needs_review"], "1")
        self.assertEqual(outputs["requires_repair"], "true")


if __name__ == "__main__":
    unittest.main()
