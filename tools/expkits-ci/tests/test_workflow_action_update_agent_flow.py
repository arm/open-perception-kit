################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import argparse
import io
import importlib.util
import os
from pathlib import Path
import tempfile
import textwrap
import urllib.error
import urllib.parse
import unittest
from unittest import mock
import zipfile

import yaml


REPO_ROOT = Path(__file__).resolve().parents[3]
WORKFLOW_FILE = REPO_ROOT / ".github/workflows/workflow-action-update-agent.yml"
REUSABLE_WORKFLOW_FILE = REPO_ROOT / ".github/workflows/workflow-action-update-agent-reusable.yml"
STABILIZER_WORKFLOW_FILE = REPO_ROOT / ".github/workflows/codex-stabilize-pr.yml"
WORKFLOW_AUDIT_FILE = REPO_ROOT / ".github/workflows/workflow-audit.yml"
CODEX_REVIEW_WORKFLOW_FILE = REPO_ROOT / ".github/workflows/codex-review.yml"
PEK_CI_WORKFLOW_FILE = REPO_ROOT / ".github/workflows/pek-ci.yml"
SONAR_WORKFLOW_FILE = REPO_ROOT / ".github/workflows/sonar.yml"
WORKFLOW_AUDIT_REPORT_SCRIPT = REPO_ROOT / "scripts/private/workflow_audit_report.py"
CODEX_REVIEW_FETCH_SCRIPT = REPO_ROOT / "codex-review/scripts/fetch-review-state.py"
CODEX_REVIEW_PUBLISH_SCRIPT = REPO_ROOT / "codex-review/scripts/publish-review.py"
CODEX_REVIEW_RUN_SCRIPT = REPO_ROOT / "codex-review/scripts/run-review.sh"
AGENT_REQUIREMENTS_FILE = REPO_ROOT / "codex-review/requirements-agent.txt"
OPENAI_AGENT_RUNNER_SCRIPT = REPO_ROOT / "scripts/private/openai_agent_runner.py"
HELPER_SCRIPT = REPO_ROOT / "scripts/private/workflow_action_update_agent.py"
HELPER_ACTION_FILE = REPO_ROOT / ".github/actions/workflow-action-update-agent-helper/action.yml"
MARKDOWN_TEMPLATE_ROOT = REPO_ROOT / ".github/ci/workflow-action-update-agent"
GOAL_TEMPLATE = MARKDOWN_TEMPLATE_ROOT / "goal.md"
CONTEXT_TEMPLATE = MARKDOWN_TEMPLATE_ROOT / "context.md"
PONYTAIL_TEMPLATE = MARKDOWN_TEMPLATE_ROOT / "ponytail-review.md"
CONSTRAINTS_TEMPLATE = MARKDOWN_TEMPLATE_ROOT / "constraints.md"
VALIDATION_TEMPLATE = MARKDOWN_TEMPLATE_ROOT / "validation.md"
PROFILE_FILE = MARKDOWN_TEMPLATE_ROOT / "profile.json"
WORKFLOW_AUDIT_PROFILE_FILE = MARKDOWN_TEMPLATE_ROOT / "workflow-audit-profile.json"
PULL_REQUEST_TEMPLATE = REPO_ROOT / ".github/PULL_REQUEST_TEMPLATE.md"
REPAIR_BRANCH = "feature/EXPKITS-4242/bot-workflow-action-update-agent-run-12345"  # pragma: allowlist secret


def load_yaml(path: Path) -> dict[str, object]:
    return yaml.load(path.read_text(encoding="utf-8"), Loader=yaml.BaseLoader)


def load_python_module(path: Path, module_name: str):
    spec = importlib.util.spec_from_file_location(module_name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load module {module_name} from {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def step_map(job: dict[str, object]) -> dict[str, dict[str, object]]:
    return {
        step["name"]: step
        for step in job.get("steps", [])
        if isinstance(step, dict) and "name" in step
    }


def build_zip_archive(files: dict[str, str]) -> bytes:
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as archive:
        for path, content in files.items():
            archive.writestr(path, content)
    return buffer.getvalue()


HELPER = load_python_module(HELPER_SCRIPT, "workflow_action_update_agent")
WORKFLOW_AUDIT_REPORT = load_python_module(WORKFLOW_AUDIT_REPORT_SCRIPT, "workflow_audit_report")
CODEX_REVIEW_FETCH = load_python_module(CODEX_REVIEW_FETCH_SCRIPT, "codex_review_fetch_review_state")
CODEX_REVIEW_PUBLISH = load_python_module(CODEX_REVIEW_PUBLISH_SCRIPT, "codex_review_publish_review")


class WorkflowActionUpdateAgentStaticTests(unittest.TestCase):
    def test_manual_wrapper_calls_reusable_workflow_with_minimal_inputs(self):
        workflow = load_yaml(WORKFLOW_FILE)
        dispatch_inputs = workflow["on"]["workflow_dispatch"]["inputs"]
        repair_job = workflow["jobs"]["run-workflow-action-update-agent"]
        stabilize_job = workflow["jobs"]["run-codex-stabilizer"]

        self.assertEqual(
            set(dispatch_inputs.keys()),
            {"source_run_id", "pr_number", "head_sha", "target_branch", "ticket_id", "profile_path"},
        )
        self.assertNotIn("workflow_run", workflow["on"])
        self.assertEqual(repair_job["uses"], "./.github/workflows/workflow-action-update-agent-reusable.yml")
        self.assertEqual(stabilize_job["uses"], "./.github/workflows/codex-stabilize-pr.yml")
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
        codex_job = workflow["jobs"]["codex-fix"]
        stabilize_job = workflow["jobs"]["stabilize-pr"]
        codex_steps = step_map(codex_job)
        stabilize_steps = step_map(stabilize_job)

        self.assertEqual(
            set(inputs.keys()),
            {"source_run_id", "target_branch", "ticket_id", "profile_path", "source_artifact_name"},
        )
        self.assertEqual(
            inputs["profile_path"]["default"],
            ".github/ci/workflow-action-update-agent/profile.json",
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
        self.assertEqual(codex_job["runs-on"], ["self-hosted", "Linux", "X64"])
        self.assertEqual(stabilize_job["runs-on"], "ubuntu-latest")
        self.assertIn("Download source artifact context", codex_steps)
        self.assertIn("Install OpenAI agent runtime", codex_steps)
        self.assertIn("Run OpenAI SDK repair agent", codex_steps)
        self.assertNotIn("Prime Codex CLI", stabilize_steps)
        self.assertNotIn("Apply deterministic workflow freshness patch", codex_steps)

        install_step = codex_steps["Install OpenAI agent runtime"]
        self.assertEqual(
            install_step["run"],
            "python3 -m venv .codex/openai-agent-venv\n"
            ".codex/openai-agent-venv/bin/python -m pip install --upgrade pip\n"
            ".codex/openai-agent-venv/bin/python -m pip install -r codex-review/requirements-agent.txt\n",
        )
        codex_step = codex_steps["Run OpenAI SDK repair agent"]
        self.assertEqual(codex_step["shell"], "bash")
        self.assertIn(
            ".codex/openai-agent-venv/bin/python scripts/private/openai_agent_runner.py run-repair",
            codex_step["run"],
        )
        self.assertIn("--prompt-file .codex/workflow-action-update-agent/goal.md", codex_step["run"])
        self.assertIn("--model \"${{ needs.prepare.outputs.codex_model }}\"", codex_step["run"])
        self.assertEqual(
            codex_step["env"]["OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS"],
            "${{ secrets.OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS }}",
        )
        self.assertEqual(
            codex_step["env"]["OPENAI_API_KEY"],
            "${{ secrets.OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS }}",
        )
        self.assertEqual(
            codex_step["env"]["OPENAI_BASE_URL"],
            "https://openai-api-proxy.geo.arm.com/api/providers/openai-eu/v1",
        )
        self.assertEqual(codex_step["env"]["OPENAI_AGENTS_DISABLE_TRACING"], "1")
        self.assertEqual(
            codex_steps["Download source artifact context"]["if"],
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
            ".github/ci/workflow-action-update-agent/profile.json",
        )
        self.assertIn("command", action["inputs"])
        self.assertIn("should_run", action["outputs"])
        self.assertIn("repair_branch", action["outputs"])
        self.assertIn("codex_model", action["outputs"])
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
            workspace_helper.resolve_repo_path(".github/ci/workflow-action-update-agent/profile.json"),
            Path(temp_dir).resolve() / ".github/ci/workflow-action-update-agent/profile.json",
        )

    def test_download_github_archive_follows_redirect_location(self):
        redirect_error = urllib.error.HTTPError(
            url="https://api.github.com/repos/Arm-Debug/amp-dev-forge/actions/artifacts/1/zip",
            code=302,
            msg="Found",
            hdrs={"Location": "https://objects.githubusercontent.com/archive.zip"},
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

    def test_codex_review_workflow_uses_openai_sdk_proxy_flow(self):
        workflow = load_yaml(CODEX_REVIEW_WORKFLOW_FILE)
        review_job = workflow["jobs"]["review"]
        review_steps = step_map(review_job)

        self.assertEqual(review_job["runs-on"], ["self-hosted", "Linux", "X64"])
        self.assertEqual(
            list(review_steps),
            [
                "Checkout pull request head",
                "Render Codex review prompt",
                "Install OpenAI agent runtime",
                "Run OpenAI SDK review",
                "Render review summary",
                "Publish review summary comment",
                "Upload review artifacts",
            ],
        )
        install_step = review_steps["Install OpenAI agent runtime"]
        self.assertEqual(
            install_step["run"],
            "python3 -m venv .codex/openai-agent-venv\n"
            ".codex/openai-agent-venv/bin/python -m pip install --upgrade pip\n"
            ".codex/openai-agent-venv/bin/python -m pip install -r codex-review/requirements-agent.txt\n",
        )
        codex_step = review_steps["Run OpenAI SDK review"]
        self.assertEqual(codex_step["shell"], "bash")
        self.assertIn(
            ".codex/openai-agent-venv/bin/python scripts/private/openai_agent_runner.py run-review",
            codex_step["run"],
        )
        self.assertIn("--schema-file codex-review/schemas/review.schema.json", codex_step["run"])
        self.assertIn("--output-file codex-review/out/review.json", codex_step["run"])
        self.assertEqual(
            codex_step["env"]["OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS"],
            "${{ secrets.OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS }}",
        )
        self.assertEqual(
            codex_step["env"]["OPENAI_API_KEY"],
            "${{ secrets.OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS }}",
        )
        self.assertEqual(
            codex_step["env"]["OPENAI_BASE_URL"],
            "https://openai-api-proxy.geo.arm.com/api/providers/openai-eu/v1",
        )
        self.assertEqual(codex_step["env"]["OPENAI_AGENTS_DISABLE_TRACING"], "1")

    def test_openai_sdk_runner_replaces_codex_action_and_cli_paths(self):
        searched_files = [
            CODEX_REVIEW_WORKFLOW_FILE,
            REUSABLE_WORKFLOW_FILE,
            STABILIZER_WORKFLOW_FILE,
            CODEX_REVIEW_RUN_SCRIPT,
        ]

        for path in searched_files:
            content = path.read_text(encoding="utf-8")
            self.assertNotIn("openai/codex-action@v1", content, path)
            self.assertNotIn("codex exec", content, path)
            self.assertIn("openai_agent_runner.py", content, path)

    def test_openai_agent_runner_uses_arm_proxy_truststore_and_tracing_contract(self):
        source = OPENAI_AGENT_RUNNER_SCRIPT.read_text(encoding="utf-8")

        self.assertIn(
            'DEFAULT_OPENAI_BASE_URL = "https://openai-api-proxy.geo.arm.com/api/providers/openai-eu/v1"',
            source,
        )
        self.assertIn('os.environ.setdefault("OPENAI_AGENTS_DISABLE_TRACING", "1")', source)
        self.assertIn(
            (
                "os.environ[\"OPENAI_API_KEY\"] = "
                "os.environ[\"OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS\"]"
            ),
            source,
        )
        truststore_import = source.index("import truststore")
        inject_call = source.index("truststore.inject_into_ssl()")
        agents_import = source.index("from agents import")
        self.assertLess(truststore_import, agents_import)
        self.assertLess(inject_call, agents_import)
        self.assertIn("class ReviewResult", source)
        self.assertIn("output_type=ReviewResult", source)

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

    def test_local_review_runner_uses_shared_sdk_script(self):
        content = CODEX_REVIEW_RUN_SCRIPT.read_text(encoding="utf-8")

        self.assertIn('agent_venv="${CODEX_REVIEW_AGENT_VENV:-.codex/openai-agent-venv}"', content)
        self.assertIn(
            'export OPENAI_API_KEY="${OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS}"',
            content,
        )
        self.assertIn(
            "OpenAI SDK review requires OPENAI_API_KEY or OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS.",
            content,
        )
        self.assertIn('python3 -m venv "${agent_venv}"', content)
        self.assertIn('"${agent_venv}/bin/python" -m pip install -r codex-review/requirements-agent.txt', content)
        self.assertIn('"${agent_venv}/bin/python" scripts/private/openai_agent_runner.py run-review', content)
        self.assertIn("--schema-file \"codex-review/schemas/review.schema.json\"", content)
        self.assertNotIn("command -v codex", content)
        self.assertNotIn("pip install --user", content)

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

    def test_stabilizer_workflow_uses_canonical_codex_review_shape(self):
        workflow = load_yaml(STABILIZER_WORKFLOW_FILE)
        call_inputs = workflow["on"]["workflow_call"]["inputs"]
        dispatch_inputs = workflow["on"]["workflow_dispatch"]["inputs"]
        job = workflow["jobs"]["stabilize"]
        steps = step_map(job)

        self.assertEqual(set(call_inputs.keys()), set(dispatch_inputs.keys()))
        self.assertEqual(job["runs-on"], ["self-hosted", "Linux", "X64"])
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
                "Upload stabilization artifacts",
            ],
        )
        snapshot_step = steps["Snapshot workflow helper bundle"]
        self.assertIn('cp -R scripts/private/. "${bundle_root}/scripts/private"', snapshot_step["run"])
        self.assertIn('cp -R codex-review/. "${bundle_root}/codex-review"', snapshot_step["run"])
        self.assertNotIn("cp scripts/private/openai_agent_runner.py", snapshot_step["run"])
        self.assertNotIn("cp codex-review/requirements-agent.txt", snapshot_step["run"])
        install_step = steps["Install OpenAI agent runtime"]
        self.assertEqual(
            install_step["run"],
            "python3 -m venv .codex/openai-agent-venv\n"
            ".codex/openai-agent-venv/bin/python -m pip install --upgrade pip\n"
            ".codex/openai-agent-venv/bin/python -m pip install -r "
            ".workflow-action-update-agent-helper/codex-review/requirements-agent.txt\n",
        )
        codex_step = steps["Run OpenAI SDK stabilization agent"]
        self.assertEqual(codex_step["shell"], "bash")
        self.assertIn(
            ".codex/openai-agent-venv/bin/python "
            ".workflow-action-update-agent-helper/scripts/private/openai_agent_runner.py run-stabilization",
            codex_step["run"],
        )
        self.assertIn("--prompt-file \"${{ inputs.context_root }}/stabilize-goal.md\"", codex_step["run"])
        self.assertEqual(
            codex_step["env"]["OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS"],
            "${{ secrets.OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS }}",
        )
        self.assertEqual(
            codex_step["env"]["OPENAI_API_KEY"],
            "${{ secrets.OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS }}",
        )
        self.assertEqual(
            codex_step["env"]["OPENAI_BASE_URL"],
            "https://openai-api-proxy.geo.arm.com/api/providers/openai-eu/v1",
        )
        self.assertEqual(codex_step["env"]["OPENAI_AGENTS_DISABLE_TRACING"], "1")
        self.assertEqual(
            steps["Resolve PR details"]["with"]["command"],
            "resolve-pr-details",
        )
        self.assertEqual(snapshot_step["shell"], "bash")
        self.assertEqual(steps["Restore workflow helper bundle"]["shell"], "bash")
        self.assertEqual(
            steps["Prepare stabilization context"]["with"]["command"],
            "prepare-stabilization-context",
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
            steps["Run stabilization validation"]["uses"],
            "./.workflow-action-update-agent-helper/.github/actions/workflow-action-update-agent-helper",
        )
        self.assertEqual(
            steps["Commit stabilization fix"]["with"]["command"],
            "commit-review-fix",
        )
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
            self.assertEqual(outputs["codex_model"], "gpt-5.3-codex")

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
            ["codex-review.yml", "workflow-audit.yml", "pek-ci.yml", "sonar.yml"],
        )

    def test_codex_review_gate_is_profile_driven(self):
        profile = HELPER.load_profile(str(PROFILE_FILE))
        validation_workflows = HELPER.profile_validation_workflows(profile)
        codex_review = next(
            item for item in validation_workflows if item["workflow_file"] == "codex-review.yml"
        )
        pek_ci = next(
            item for item in validation_workflows if item["workflow_file"] == "pek-ci.yml"
        )
        sonar = next(
            item for item in validation_workflows if item["workflow_file"] == "sonar.yml"
        )

        self.assertEqual(codex_review["workflow_name"], "Codex Review")
        self.assertEqual(codex_review["review_state_script"], "codex-review/scripts/fetch-review-state.py")
        self.assertEqual(codex_review["allowed_review_recommendations"], ["approve"])
        self.assertEqual(codex_review["workflow_dispatch_inputs"], {"base_ref": "origin/{target_branch}"})
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
        self.assertEqual(profile["codex_model"], "gpt-5.3-codex")

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

    def test_codex_review_publish_and_fetch_scripts_share_structured_state(self):
        review = {
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

        markdown = CODEX_REVIEW_PUBLISH.format_markdown(
            review,
            run_id="28000000001",
            head_sha="deadbeef",
        )
        inline_comment = CODEX_REVIEW_PUBLISH.build_inline_comment_body(
            review["findings"][0],
            run_id="28000000001",
        )

        self.assertIn(CODEX_REVIEW_PUBLISH.MARKER, markdown)
        self.assertIn(CODEX_REVIEW_PUBLISH.STATE_MARKER, markdown)
        self.assertIn(CODEX_REVIEW_PUBLISH.INLINE_MARKER, inline_comment)
        self.assertIn(CODEX_REVIEW_PUBLISH.INLINE_STATE_MARKER, inline_comment)
        self.assertEqual(CODEX_REVIEW_FETCH.EMPTY_STATE["overall_recommendation"], "")

    def test_codex_review_fetch_accepts_default_github_actions_authors(self):
        with mock.patch.dict(os.environ, {}, clear=True):
            author_logins = CODEX_REVIEW_FETCH.allowed_author_logins()

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
            "${{ inputs.repair_profile_path || '.github/ci/workflow-action-update-agent/workflow-audit-profile.json' }}",
        )
        self.assertEqual(repair_job["permissions"]["actions"], "write")
        self.assertIn("github.event.inputs.stabilize_pr_number == ''", report_job["if"])
        self.assertIn("github.event.inputs.stabilize_pr_number == ''", repair_job["if"])
        self.assertIn("needs.workflow-dependency-freshness.outputs.requires_repair == 'true'", repair_job["if"])
        self.assertIn("github.event_name == 'schedule'", repair_job["if"])
        self.assertIn("github.event_name == 'workflow_dispatch'", repair_job["if"])
        self.assertEqual(stabilize_job["uses"], "./.github/workflows/codex-stabilize-pr.yml")
        self.assertEqual(
            stabilize_job["if"],
            "${{ github.event_name == 'workflow_dispatch' && github.event.inputs.stabilize_pr_number != '' }}",
        )
        self.assertEqual(stabilize_job["with"]["pr_number"], "${{ github.event.inputs.stabilize_pr_number }}")
        self.assertEqual(stabilize_job["with"]["head_sha"], "${{ github.event.inputs.stabilize_head_sha || '' }}")

    def test_wait_for_review_state_returns_observed_recommendation(self):
        with mock.patch.object(
            HELPER,
            "read_review_state",
            return_value={
                "run_id": "28000000001",
                "head_sha": "deadbeef",
                "overall_recommendation": "comment",
            },
        ):
            review_state = HELPER.wait_for_review_state(
                pr_number="123",
                workflow_name="Codex Review",
                review_state_script="codex-review/scripts/fetch-review-state.py",
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
                        workflow_name="Codex Review",
                        review_state_script="codex-review/scripts/fetch-review-state.py",
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
                    workflow_name="Codex Review",
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
                workflow_file="codex-review.yml",
                repair_branch=REPAIR_BRANCH,
                head_sha="deadbeef",
            )

        self.assertEqual(run_id, "28235500001")

    def test_ensure_allowed_review_recommendation_rejects_requested_changes(self):
        with self.assertRaisesRegex(RuntimeError, "request_changes"):
            HELPER.ensure_allowed_review_recommendation(
                pr_number="123",
                workflow_name="Codex Review",
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
            dispatch_stabilizer_workflow.assert_called_once()
            self.assertEqual(read_pr_details.call_args_list, [mock.call("123"), mock.call("123")])
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
            self.assertEqual(outputs["codex_model"], "gpt-5.3-codex")
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

        self.assertEqual(run_command.call_args.args[0][0:2], ["python3", str(CODEX_REVIEW_PUBLISH_SCRIPT)])
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
