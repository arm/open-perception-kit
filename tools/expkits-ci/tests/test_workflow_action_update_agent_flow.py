################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import argparse
import importlib.util
import os
from pathlib import Path
import tempfile
import textwrap
import unittest
from unittest import mock

import yaml


REPO_ROOT = Path(__file__).resolve().parents[3]
WORKFLOW_FILE = REPO_ROOT / ".github/workflows/workflow-action-update-agent.yml"
REUSABLE_WORKFLOW_FILE = REPO_ROOT / ".github/workflows/workflow-action-update-agent-reusable.yml"
WORKFLOW_AUDIT_FILE = REPO_ROOT / ".github/workflows/workflow-audit.yml"
CODEX_REVIEW_WORKFLOW_FILE = REPO_ROOT / ".github/workflows/codex-review.yml"
WORKFLOW_AUDIT_REPORT_SCRIPT = REPO_ROOT / "scripts/private/workflow_audit_report.py"
CODEX_REVIEW_FETCH_SCRIPT = REPO_ROOT / "codex-review/scripts/fetch-review-state.py"
CODEX_REVIEW_PUBLISH_SCRIPT = REPO_ROOT / "codex-review/scripts/publish-review.py"
CI_README = REPO_ROOT / ".github/CI-README.md"
HELPER_SCRIPT = REPO_ROOT / "scripts/private/workflow_action_update_agent.py"
HELPER_ACTION_FILE = REPO_ROOT / ".github/actions/workflow-action-update-agent-helper/action.yml"
OPENAI_CODEX_RUN_ACTION_FILE = REPO_ROOT / ".github/actions/openai-codex-run/action.yml"
MARKDOWN_TEMPLATE_ROOT = REPO_ROOT / ".github/ci/workflow-action-update-agent"
GOAL_TEMPLATE = MARKDOWN_TEMPLATE_ROOT / "goal.md"
PONYTAIL_TEMPLATE = MARKDOWN_TEMPLATE_ROOT / "ponytail-review.md"
CONSTRAINTS_TEMPLATE = MARKDOWN_TEMPLATE_ROOT / "constraints.md"
VALIDATION_TEMPLATE = MARKDOWN_TEMPLATE_ROOT / "validation.md"
PROFILE_FILE = MARKDOWN_TEMPLATE_ROOT / "profile.json"
WORKFLOW_AUDIT_PROFILE_FILE = MARKDOWN_TEMPLATE_ROOT / "workflow-audit-profile.json"
PULL_REQUEST_TEMPLATE = REPO_ROOT / ".github/PULL_REQUEST_TEMPLATE.md"
FRESHNESS_WORKFLOW_FILE = REPO_ROOT / ".github/workflows/workflow-action-update-agent-freshness.yml"
CHANGED_VALIDATION_WORKFLOW_FILE = REPO_ROOT / ".github/workflows/workflow-changed-validation.yml"
FRESHNESS_PATCH_SCRIPT = REPO_ROOT / "scripts/private/apply_workflow_freshness_updates.py"
CHANGED_WORKFLOW_VALIDATION_SCRIPT = REPO_ROOT / "scripts/private/validate_changed_workflows.py"
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


HELPER = load_python_module(HELPER_SCRIPT, "workflow_action_update_agent")
WORKFLOW_AUDIT_REPORT = load_python_module(WORKFLOW_AUDIT_REPORT_SCRIPT, "workflow_audit_report")
CODEX_REVIEW_FETCH = load_python_module(CODEX_REVIEW_FETCH_SCRIPT, "codex_review_fetch_review_state")
CODEX_REVIEW_PUBLISH = load_python_module(CODEX_REVIEW_PUBLISH_SCRIPT, "codex_review_publish_review")


class WorkflowActionUpdateAgentStaticTests(unittest.TestCase):
    def test_dispatch_wrapper_calls_reusable_workflow_with_minimal_inputs(self):
        workflow = load_yaml(WORKFLOW_FILE)
        dispatch_inputs = workflow["on"]["workflow_dispatch"]["inputs"]
        workflow_run = workflow["on"]["workflow_run"]
        job = workflow["jobs"]["run-workflow-action-update-agent"]

        self.assertEqual(
            set(dispatch_inputs.keys()),
            {"source_run_id", "target_branch", "ticket_id", "profile_path"},
        )
        self.assertEqual(
            workflow_run["workflows"],
            ["Perception Experience Kit CI Pipeline"],
        )
        self.assertEqual(job["uses"], "./.github/workflows/workflow-action-update-agent-reusable.yml")
        self.assertEqual(
            set(job["with"].keys()),
            {"source_run_id", "target_branch", "ticket_id", "profile_path"},
        )
        self.assertNotIn("source_workflow_conclusion", job["with"])
        self.assertNotIn("source_head_branch", job["with"])
        self.assertNotIn("source_head_repository", job["with"])
        self.assertEqual(job["secrets"], "inherit")

    def test_reusable_workflow_uses_profile_and_composite_action(self):
        workflow = load_yaml(REUSABLE_WORKFLOW_FILE)
        workflow_text = REUSABLE_WORKFLOW_FILE.read_text(encoding="utf-8")
        inputs = workflow["on"]["workflow_call"]["inputs"]

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
                "wait-for-pr-workflows",
                "merge-pr",
            },
        )
        self.assertNotIn("python3 scripts/private/workflow_action_update_agent.py", workflow_text)
        self.assertNotIn("source_workflow_conclusion", workflow_text)
        self.assertNotIn("source_head_branch", workflow_text)
        self.assertNotIn("source_head_repository", workflow_text)
        self.assertIn("./.github/actions/openai-codex-run", workflow_text)
        self.assertIn("allow-bots: true", workflow_text)
        self.assertIn("allow-bot-users: github-actions[bot]", workflow_text)
        self.assertIn("Require Codex proxy credentials", workflow_text)
        self.assertIn("OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS", workflow_text)
        self.assertNotIn("OPENAI_API_KEY_VALUE", workflow_text)
        self.assertIn("Apply deterministic workflow freshness patch", workflow_text)
        self.assertIn("inputs.source_artifact_name == 'workflow-dependency-freshness'", workflow_text)
        self.assertIn("test_workflow_freshness_patch.py", workflow_text)
        self.assertIn(
            "responses-api-endpoint: https://openai-api-proxy.geo.arm.com/api/providers/openai/v1/responses", workflow_text)
        self.assertIn("model: ${{ needs.prepare.outputs.codex_model }}", workflow_text)
        self.assertIn("effort: ${{ needs.prepare.outputs.codex_effort }}", workflow_text)
        self.assertIn("safety-strategy: unsafe", workflow_text)
        self.assertIn("Download source artifact context", workflow_text)

    def test_helper_action_exposes_structured_outputs(self):
        action = load_yaml(HELPER_ACTION_FILE)
        action_text = HELPER_ACTION_FILE.read_text(encoding="utf-8")

        self.assertEqual(action["runs"]["using"], "composite")
        self.assertEqual(
            action["inputs"]["profile-path"]["default"],
            ".github/ci/workflow-action-update-agent/profile.json",
        )
        self.assertIn("command", action["inputs"])
        self.assertIn("should_run", action["outputs"])
        self.assertIn("repair_branch", action["outputs"])
        self.assertIn("codex_model", action["outputs"])
        self.assertIn("codex_effort", action["outputs"])
        self.assertIn("has_changes", action["outputs"])
        self.assertIn("head_sha", action["outputs"])
        self.assertIn("pr_number", action["outputs"])
        self.assertIn('case "${command}" in', action_text)
        self.assertIn('--pr-number "${{ inputs.pr-number }}"', action_text)

    def test_openai_codex_run_action_retries_with_workflow_npm_config(self):
        action = load_yaml(OPENAI_CODEX_RUN_ACTION_FILE)
        action_text = OPENAI_CODEX_RUN_ACTION_FILE.read_text(encoding="utf-8")

        self.assertEqual(action["runs"]["using"], "composite")
        self.assertEqual(action["inputs"]["npm-userconfig-path"]["default"], "")
        self.assertEqual(action["inputs"]["output-schema-file"]["default"], "")
        self.assertIn("openai/codex-action@v1", action_text)
        self.assertIn("output-schema-file", action_text)
        self.assertIn("NPM_CONFIG_USERCONFIG", action_text)
        self.assertIn("codex-home", action_text)
        self.assertIn("openai-codex-run/attempt-1", action_text)
        self.assertIn("openai-codex-run/attempt-2", action_text)
        self.assertIn("openai-codex-run/attempt-3", action_text)
        self.assertIn("openai-codex-run/attempt-4", action_text)
        self.assertIn("openai-codex-run/attempt-5", action_text)
        self.assertIn("inputs.npm-userconfig-path != ''", action_text)
        self.assertIn("inputs.safety-strategy == 'drop-sudo' && 'unsafe' || inputs.safety-strategy", action_text)
        self.assertIn("continue-on-error: true", action_text)
        self.assertIn("sleep 15", action_text)
        self.assertIn("sleep 30", action_text)
        self.assertIn("sleep 60", action_text)
        self.assertIn("sleep 120", action_text)

    def test_codex_review_workflow_prefers_direct_openai_key_with_proxy_fallback(self):
        workflow_text = CODEX_REVIEW_WORKFLOW_FILE.read_text(encoding="utf-8")

        self.assertNotIn("Resolve Codex review credentials", workflow_text)
        self.assertNotIn("OPENAI_API_KEY_VALUE", workflow_text)
        self.assertNotIn("NPM_CONFIG_USERCONFIG", workflow_text)
        self.assertIn("runs-on: ubuntu-latest", workflow_text)
        self.assertIn("safety-strategy: unsafe", workflow_text)
        self.assertIn("./.github/actions/openai-codex-run", workflow_text)
        self.assertIn(
            "openai-api-key: ${{ secrets.OPENAI_API_KEY || secrets.OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS }}",
            workflow_text,
        )
        self.assertIn(
            "responses-api-endpoint: ${{ secrets.OPENAI_API_KEY && '' || 'https://openai-api-proxy.geo.arm.com/api/providers/openai/v1/responses' }}",
            workflow_text,
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
            self.assertEqual(outputs["codex_effort"], "")

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

        self.assertEqual(codex_review["workflow_name"], "Codex Review")
        self.assertEqual(codex_review["review_state_script"], "codex-review/scripts/fetch-review-state.py")
        self.assertEqual(codex_review["allowed_review_recommendations"], ["approve", "comment"])
        self.assertEqual(profile["codex_model"], "gpt-5.3-codex")
        self.assertEqual(profile["codex_effort"], "")

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
        workflow_text = WORKFLOW_AUDIT_FILE.read_text(encoding="utf-8")
        dispatch_inputs = workflow["on"]["workflow_dispatch"]["inputs"]
        report_job = workflow["jobs"]["workflow-dependency-freshness"]
        repair_job = workflow["jobs"]["repair-workflow-dependency-freshness"]

        self.assertEqual(
            set(dispatch_inputs.keys()),
            {"ticket_id", "repair_profile_path"},
        )
        self.assertIn("requires_repair", report_job["outputs"])
        self.assertIn("behind_latest", report_job["outputs"])
        self.assertIn("needs_review", report_job["outputs"])
        self.assertIn("--github-output", workflow_text)
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
        self.assertIn("needs.workflow-dependency-freshness.outputs.requires_repair == 'true'", repair_job["if"])
        self.assertIn("github.event_name == 'schedule' || github.event_name == 'workflow_dispatch'", repair_job["if"])

    def test_wait_for_review_state_accepts_non_blocking_recommendation(self):
        with mock.patch.object(
            HELPER,
            "read_review_state",
            return_value={
                "run_id": "28000000001",
                "head_sha": "deadbeef",
                "overall_recommendation": "comment",
            },
        ):
            HELPER.wait_for_review_state(
                pr_number="123",
                workflow_name="Codex Review",
                review_state_script="codex-review/scripts/fetch-review-state.py",
                allowed_review_recommendations=["approve", "comment"],
                expected_run_id="28000000001",
                head_sha="deadbeef",
            )

    def test_wait_for_review_state_rejects_requested_changes(self):
        with mock.patch.object(
            HELPER,
            "read_review_state",
            return_value={
                "run_id": "28000000001",
                "head_sha": "deadbeef",
                "overall_recommendation": "request_changes",
            },
        ):
            with self.assertRaisesRegex(RuntimeError, "request_changes"):
                HELPER.wait_for_review_state(
                    pr_number="123",
                    workflow_name="Codex Review",
                    review_state_script="codex-review/scripts/fetch-review-state.py",
                    allowed_review_recommendations=["approve", "comment"],
                    expected_run_id="28000000001",
                    head_sha="deadbeef",
                )

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

    def test_core_assets_exist(self):
        self.assertTrue(MARKDOWN_TEMPLATE_ROOT.is_dir())
        self.assertTrue(GOAL_TEMPLATE.is_file())
        self.assertTrue(PONYTAIL_TEMPLATE.is_file())
        self.assertTrue(CONSTRAINTS_TEMPLATE.is_file())
        self.assertTrue(VALIDATION_TEMPLATE.is_file())
        self.assertTrue(PROFILE_FILE.is_file())
        self.assertTrue(WORKFLOW_AUDIT_PROFILE_FILE.is_file())
        self.assertTrue(HELPER_ACTION_FILE.is_file())
        self.assertTrue(PULL_REQUEST_TEMPLATE.is_file())

    def test_ci_readme_documents_core_legos_only(self):
        readme = CI_README.read_text(encoding="utf-8")

        self.assertIn(".github/workflows/workflow-action-update-agent.yml", readme)
        self.assertIn(".github/workflows/workflow-action-update-agent-reusable.yml", readme)
        self.assertIn(".github/workflows/workflow-audit.yml", readme)
        self.assertIn(".github/workflows/codex-review.yml", readme)
        self.assertIn(".github/actions/workflow-action-update-agent-helper/", readme)
        self.assertIn(".github/ci/workflow-action-update-agent/profile.json", readme)
        self.assertIn(".github/ci/workflow-action-update-agent/workflow-audit-profile.json", readme)
        self.assertIn("single nightly pipeline", readme)
        self.assertIn("building blocks", readme)
        self.assertIn("OPENAI_PROXY_KEY_FOR_SELF_HOSTED_RUNNERS", readme)
        self.assertIn("EXPKITS_AGENT_TOKEN", readme)
        self.assertIn("waits for the profile-defined validation workflows", readme)
        self.assertNotIn("workflow-action-update-agent-freshness.yml", readme)
        self.assertNotIn("validate_changed_workflows.py", readme)
        self.assertNotIn("workflow-changed-validation.yml", readme)
        self.assertNotIn("Runs on pull requests, manual dispatch, and nightly schedule.", readme)

    def test_experiment_files_are_absent_from_minimal_branch(self):
        self.assertFalse(FRESHNESS_WORKFLOW_FILE.exists())
        self.assertFalse(CHANGED_VALIDATION_WORKFLOW_FILE.exists())
        self.assertTrue(FRESHNESS_PATCH_SCRIPT.exists())
        self.assertFalse(CHANGED_WORKFLOW_VALIDATION_SCRIPT.exists())


if __name__ == "__main__":
    unittest.main()
