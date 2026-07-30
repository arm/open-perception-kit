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
AGENT_REPAIR_SOURCE_RUN_WORKFLOW_FILE = REPO_ROOT / ".github/workflows/agent-repair-source-run.yml"
AGENT_REPAIR_SOURCE_RUN_WORKER_FILE = REPO_ROOT / ".github/workflows/agent-repair-source-run-worker.yml"
AGENT_STABILIZE_PR_WORKER_FILE = REPO_ROOT / ".github/workflows/agent-stabilize-pr-worker.yml"
AGENT_STABILIZE_PR_LABEL_WORKFLOW_FILE = REPO_ROOT / ".github/workflows/agent-stabilize-pr-on-label.yml"
WORKFLOW_AUDIT_FILE = REPO_ROOT / ".github/workflows/workflow-audit.yml"
AGENT_REVIEW_WORKFLOW_FILE = REPO_ROOT / ".github/workflows/agent-review.yml"
PEK_CI_WORKFLOW_FILE = REPO_ROOT / ".github/workflows/pek-ci.yml"
SONAR_WORKFLOW_FILE = REPO_ROOT / ".github/workflows/sonar.yml"
WORKFLOW_AUDIT_REPORT_SCRIPT = REPO_ROOT / "scripts/private/workflow_audit_report.py"
QUALITY_CHECKS_SCRIPT = REPO_ROOT / "tools/expkits-ci/expkits_ci/quality_checks.py"
AGENT_REVIEW_ROOT = REPO_ROOT / ".github/agent-runtime/review"
AGENT_REVIEW_FETCH_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/fetch.py"
AGENT_REVIEW_PUBLISH_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/publish.py"
AGENT_REVIEW_CONTEXT_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/context.py"
AGENT_REVIEW_COMMENTS_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/comments.py"
AGENT_REVIEW_DIFF_ANCHORS_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/diff_anchors.py"
AGENT_REVIEW_GITHUB_PUBLISH_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/github_publish.py"
AGENT_REVIEW_MARKDOWN_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/markdown.py"
AGENT_REVIEW_STATE_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/state.py"
AGENT_REVIEW_INSTRUCTIONS_FILE = REPO_ROOT / ".github/agent-runtime/review/instructions.md"
AGENT_REQUIREMENTS_FILE = REPO_ROOT / ".github/agent-runtime/runtime/requirements-openai-agents.txt"
AGENT_MYPY_CONFIG_FILE = REPO_ROOT / "tools/expkits-ci/agent-workflows-mypy.ini"
AGENT_MODEL_CONFIG_FILE = REPO_ROOT / ".github/agent-runtime/runtime/agent-models.json"
AGENT_TASK_CONFIG_FILE = REPO_ROOT / ".github/agent-runtime/runtime/agent-tasks.json"
OPENAI_AGENT_RUNNER_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/openai_agent_runner.py"
OPENAI_AGENT_WORKFLOW_TASK_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/tasks/base.py"
OPENAI_AGENT_TASKS_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/tasks/configured.py"
OPENAI_AGENT_CONTRACTS_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/contracts.py"
GITHUB_ACTIONS_SCRIPT = REPO_ROOT / "scripts/private/github_actions.py"
OPENAI_AGENT_REPO_TOOLS_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/tools/repo.py"
OPENAI_AGENT_SHELL_TOOLS_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/tools/shell.py"
OPENAI_AGENT_PATH_TOOLS_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/tools/paths.py"
OPENAI_AGENT_RUNTIME_CONTEXT_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/runtime_context.py"
OPENAI_AGENT_SDK_RUNTIME_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/sdk_runtime.py"
OPENAI_AGENT_TASK_CONFIG_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/config/task.py"
OPENAI_AGENT_TASK_ESTIMATOR_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/tasks/estimator.py"
OPENAI_AGENT_MODEL_CONFIG_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/config/model.py"
OPENAI_AGENT_REVIEW_OUTPUT_SCRIPT = REPO_ROOT / "scripts/private/agent_runtime/review/output_filter.py"
REPAIR_CLI_SCRIPT = REPO_ROOT / "scripts/private/agent_repair_orchestrator/cli.py"
REPAIR_PROFILE_SCRIPT = REPO_ROOT / "scripts/private/agent_repair_orchestrator/profile.py"
REPAIR_PATHS_SCRIPT = REPO_ROOT / "scripts/private/agent_repair_orchestrator/paths.py"
COMMON_PROCESS_SCRIPT = REPO_ROOT / "scripts/private/agent_workflow_common/process.py"
COMMON_PROFILE_SCRIPT = REPO_ROOT / "scripts/private/agent_workflow_common/profile.py"
COMMON_TASK_REFS_SCRIPT = REPO_ROOT / "scripts/private/agent_workflow_common/task_refs.py"
REPAIR_TEMPLATES_SCRIPT = REPO_ROOT / "scripts/private/agent_repair_orchestrator/templates.py"
COMMON_VALIDATION_SCRIPT = REPO_ROOT / "scripts/private/agent_workflow_common/validation.py"
COMMON_REVIEW_WORKFLOW_SCRIPT = REPO_ROOT / "scripts/private/agent_workflow_common/review_workflow.py"
STABILIZATION_ORCHESTRATOR_SCRIPT = REPO_ROOT / "scripts/private/agent_stabilization_orchestrator/stabilization.py"
STABILIZATION_PROFILE_SCRIPT = REPO_ROOT / "scripts/private/agent_stabilization_orchestrator/profile.py"
STABILIZATION_PATHS_SCRIPT = REPO_ROOT / "scripts/private/agent_stabilization_orchestrator/paths.py"
STABILIZATION_TEMPLATES_SCRIPT = REPO_ROOT / "scripts/private/agent_stabilization_orchestrator/templates.py"
SOURCE_RUN_REPAIR_ROOT = REPO_ROOT / ".github/agent-runtime/source-run-repair"
PR_STABILIZATION_ROOT = REPO_ROOT / ".github/agent-runtime/pr-stabilization"
WORKFLOW_POLICY_ROOT = REPO_ROOT / ".github/agent-runtime/workflow-policy"
PROMPT_TEMPLATE_ROOT = SOURCE_RUN_REPAIR_ROOT / "prompts"
STABILIZATION_PROMPT_TEMPLATE_ROOT = PR_STABILIZATION_ROOT / "prompts"
PROFILE_ROOT = SOURCE_RUN_REPAIR_ROOT / "profiles"
STABILIZATION_PROFILE_ROOT = PR_STABILIZATION_ROOT / "profiles"
STABILIZE_GOAL_TEMPLATE = STABILIZATION_PROMPT_TEMPLATE_ROOT / "stabilize-goal.md.in"
CONTEXT_TEMPLATE = PROMPT_TEMPLATE_ROOT / "context.md"
SOURCE_RUN_REPAIR_PROFILE_FILE = PROFILE_ROOT / "profile.json"
STABILIZATION_PROFILE_FILE = STABILIZATION_PROFILE_ROOT / "profile.json"
WORKFLOW_DEPENDENCY_FRESHNESS_PROFILE_FILE = PROFILE_ROOT / "workflow-dependency-freshness.json"
SOURCE_RUN_REPAIR_AGENTS_FILE = SOURCE_RUN_REPAIR_ROOT / "AGENTS.md"
REPAIR_HELPER_AGENTS_FILE = REPAIR_CLI_SCRIPT.parent / "AGENTS.md"
STABILIZATION_ORCHESTRATOR_AGENTS_FILE = STABILIZATION_ORCHESTRATOR_SCRIPT.parent / "AGENTS.md"
COMMON_WORKFLOW_AGENTS_FILE = COMMON_PROCESS_SCRIPT.parent / "AGENTS.md"
SAMPLE_TASK_REF = "TASK-1"
SAMPLE_PR_NUMBER = "101"
SAMPLE_SOURCE_RUN_ID = "12345"
REPAIR_BRANCH = f"feature/{SAMPLE_TASK_REF}/bot-agent-repair-source-run-{SAMPLE_SOURCE_RUN_ID}"
HEAD_BRANCH = REPAIR_BRANCH
OPENAI_AGENT_RUNNER_LABEL = "self-hosted-ubuntu-latest"
OPENAI_REVIEW_MAX_TURNS = 90
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
    module_path = str(REPAIR_CLI_SCRIPT.parent.parent)
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
    class FakeAgent:
        def __class_getitem__(cls, _item):
            return cls

        def __init__(self, **kwargs: object) -> None:
            for name, value in kwargs.items():
                setattr(self, name, value)

    class FakeRunConfig:
        def __init__(self, **kwargs: object) -> None:
            for name, value in kwargs.items():
                setattr(self, name, value)

    class FakeModelSettings:
        def __init__(self, **kwargs: object) -> None:
            self.temperature = kwargs.pop("temperature", None)
            self.verbosity = kwargs.pop("verbosity", None)
            for name, value in kwargs.items():
                setattr(self, name, value)

    class FakeReasoning:
        def __init__(self, **kwargs: object) -> None:
            for name, value in kwargs.items():
                setattr(self, name, value)

    class FakeRunContextWrapper:
        def __init__(self, context: object) -> None:
            self.context = context

        def __class_getitem__(cls, _item):
            return cls

    class FakeRunner:
        @classmethod
        async def run(cls, *_args: object, **_kwargs: object):
            raise AssertionError("FakeRunner.run must be mocked by the test.")

    fake_agents = types.SimpleNamespace(
        Agent=FakeAgent,
        ModelSettings=FakeModelSettings,
        RunConfig=FakeRunConfig,
        RunContextWrapper=FakeRunContextWrapper,
        Runner=FakeRunner,
        function_tool=lambda function: function,
    )
    fake_truststore = types.SimpleNamespace(inject_into_ssl=lambda: None)
    fake_openai = types.ModuleType("openai")
    fake_openai.__path__ = []  # type: ignore[attr-defined]
    fake_openai_types = types.ModuleType("openai.types")
    fake_openai_types.__path__ = []  # type: ignore[attr-defined]
    fake_openai_shared = types.ModuleType("openai.types.shared")
    fake_openai_shared.Reasoning = FakeReasoning  # type: ignore[attr-defined]
    fake_pydantic = types.SimpleNamespace(
        BaseModel=object,
        ConfigDict=lambda **_kwargs: {},
        Field=lambda *args, **_kwargs: args[0] if args else None,
    )
    module_path = str(OPENAI_AGENT_RUNNER_SCRIPT.parent.parent)
    with mock.patch.dict(
        sys.modules,
        {
            "agents": fake_agents,
            "openai": fake_openai,
            "openai.types": fake_openai_types,
            "openai.types.shared": fake_openai_shared,
            "truststore": fake_truststore,
            "pydantic": fake_pydantic,
        },
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
GITHUB_ACTIONS = load_python_module(GITHUB_ACTIONS_SCRIPT, "github_actions")
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
AGENT_REVIEW_CONTEXT = load_agent_workflow_module(
    AGENT_REVIEW_CONTEXT_SCRIPT,
    "agent_runtime.review.context",
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
REPAIR_PATHS = load_workflow_helper_module("agent_repair_orchestrator.paths")
COMMON_PROCESS = load_workflow_helper_module("agent_workflow_common.process")
COMMON_PROFILE = load_workflow_helper_module("agent_workflow_common.profile")
REPAIR_PROFILE = load_workflow_helper_module("agent_repair_orchestrator.profile")
COMMON_REVIEW_WORKFLOW = load_workflow_helper_module("agent_workflow_common.review_workflow")
COMMON_TASK_REFS = load_workflow_helper_module("agent_workflow_common.task_refs")
REPAIR_TEMPLATES = load_workflow_helper_module("agent_repair_orchestrator.templates")
COMMON_VALIDATION = load_workflow_helper_module("agent_workflow_common.validation")
REPAIR_ORCHESTRATOR = load_workflow_helper_module("agent_repair_orchestrator.repair")
STABILIZATION_ORCHESTRATOR = load_workflow_helper_module("agent_stabilization_orchestrator.stabilization")
STABILIZATION_PATHS = load_workflow_helper_module("agent_stabilization_orchestrator.paths")
STABILIZATION_PROFILE = load_workflow_helper_module("agent_stabilization_orchestrator.profile")
STABILIZATION_TEMPLATES = load_workflow_helper_module("agent_stabilization_orchestrator.templates")
