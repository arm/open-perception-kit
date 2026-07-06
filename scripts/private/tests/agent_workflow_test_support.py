################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from email.message import Message
import importlib
import importlib.util
import io
from pathlib import Path
import sys
import types
from typing import Any
from unittest import mock
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
AGENT_REVIEW_PROMPT_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/prompt.py"
AGENT_REVIEW_COMMENTS_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/comments.py"
AGENT_REVIEW_DIFF_ANCHORS_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/diff_anchors.py"
AGENT_REVIEW_GITHUB_PUBLISH_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/github_publish.py"
AGENT_REVIEW_MARKDOWN_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/markdown.py"
AGENT_REVIEW_STATE_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/state.py"
AGENT_REVIEW_PROMPT_TEMPLATE = REPO_ROOT / ".github/agent-runtime/review/prompts/review.md.in"
AGENT_REQUIREMENTS_FILE = REPO_ROOT / ".github/agent-runtime/runtime/requirements-openai-agents.txt"
AGENT_MYPY_CONFIG_FILE = REPO_ROOT / "tools/expkits-ci/agent-workflows-mypy.ini"
AGENT_MODEL_CONFIG_FILE = REPO_ROOT / ".github/agent-runtime/runtime/agent-models.json"
AGENT_TASK_CONFIG_FILE = REPO_ROOT / ".github/agent-runtime/runtime/agent-tasks.json"
OPENAI_AGENT_RUNNER_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/openai_agent_runner.py"
OPENAI_AGENT_WORKFLOW_TASK_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/tasks/base.py"
OPENAI_AGENT_TASKS_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/tasks/configured.py"
OPENAI_AGENT_CONTRACTS_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/contracts.py"
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
HELPER_SCRIPT = REPO_ROOT / "scripts/private/workflow_action_update_agent/cli.py"
HELPER_RUNTIME_SCRIPT = REPO_ROOT / "scripts/private/workflow_action_update_agent/runtime.py"
HELPER_STABILIZATION_SCRIPT = REPO_ROOT / "scripts/private/workflow_action_update_agent/stabilization.py"
WORKFLOW_AUTOMATION_ROOT = REPO_ROOT / ".github/agent-runtime/workflow-action-update-agent"
PROMPT_TEMPLATE_ROOT = WORKFLOW_AUTOMATION_ROOT / "prompts"
PROFILE_ROOT = WORKFLOW_AUTOMATION_ROOT / "profiles"
STABILIZE_GOAL_TEMPLATE = PROMPT_TEMPLATE_ROOT / "stabilize-goal.md.in"
CONTEXT_TEMPLATE = PROMPT_TEMPLATE_ROOT / "context.md"
PROFILE_FILE = PROFILE_ROOT / "profile.json"
WORKFLOW_AUDIT_PROFILE_FILE = PROFILE_ROOT / "workflow-audit-profile.json"
WORKFLOW_AUTOMATION_AGENTS_FILE = WORKFLOW_AUTOMATION_ROOT / "AGENTS.md"
HELPER_AGENTS_FILE = HELPER_SCRIPT.parent / "AGENTS.md"
SAMPLE_TASK_REF = "TASK-1"
SAMPLE_PR_NUMBER = "101"
SAMPLE_SOURCE_RUN_ID = "12345"
REPAIR_BRANCH = f"feature/{SAMPLE_TASK_REF}/bot-workflow-action-update-agent-run-{SAMPLE_SOURCE_RUN_ID}"
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
OPENAI_AGENT_RUNTIME_CONTEXT = load_agent_workflow_module(
    OPENAI_AGENT_RUNTIME_CONTEXT_SCRIPT,
    "agent_runtime.runtime_context",
)
HELPER_RUNTIME = load_workflow_helper_module("workflow_action_update_agent.runtime")
HELPER_REPAIR = load_workflow_helper_module("workflow_action_update_agent.repair")
HELPER_GITHUB_WORKFLOWS = load_workflow_helper_module("workflow_action_update_agent.github_workflows")
HELPER_STABILIZATION = load_workflow_helper_module("workflow_action_update_agent.stabilization")
