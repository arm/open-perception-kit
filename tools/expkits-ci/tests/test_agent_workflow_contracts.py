################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "scripts/private"))
from test_support.agent_workflow import load_quality_checks_module  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[3]


class AgentWorkflowBehaviorTests(unittest.TestCase):
    def test_ci_labels_dispatch_conflicted_pr_head(self):
        workflow = (REPO_ROOT / ".github/workflows/pek-ci-label.yml").read_text()

        for label in (
            "run-pek-ci",
            "run-macos-ci",
            "run-python-audit",
            "run-docker-scout",
            "run-workflow-audit",
        ):
            with self.subTest(label=label):
                self.assertIn(label, workflow)

        for contract in (
            "pull_request_target:",
            "workflow_run:",
            "permissions: {}",
            "github.event.pull_request.head.repo.full_name == github.repository",
            'if [ "$mergeable" = true ]; then',
            "gh workflow run pek-ci.yml",
            '--ref "$head_ref"',
            '-f pr_head_sha="$head_sha"',
            '-f pr_label="$PR_LABEL"',
            "checks=label",
            'if [ "$PR_LABEL" = run-pek-ci ]; then',
            "statuses/${head_sha}",
            "statuses/${HEAD_SHA}",
            "PEK CI (head)",
            'context="$DISPLAY_TITLE"',
            'target_url="$RUN_URL"',
        ):
            with self.subTest(contract=contract):
                self.assertIn(contract, workflow)

    def test_manual_pr_valgrind_waits_for_missing_baseline(self):
        pek_ci = (REPO_ROOT / ".github/workflows/pek-ci.yml").read_text()
        wait_condition = pek_ci.split(
            "- name: Wait for missing Valgrind baseline in GHCR", 1
        )[1].split("id: waited_valgrind_baseline", 1)[0]
        self.assertIn("needs.build-ci-image.outputs.pr_context == 'true'", wait_condition)
        self.assertNotIn("github.event_name == 'pull_request'", wait_condition)

    def test_manual_pr_all_checks_preserves_macos_label_gate(self):
        pek_ci = (REPO_ROOT / ".github/workflows/pek-ci.yml").read_text()
        macos_condition = pek_ci.split("  macos-nightly-test:", 1)[1].split(
            "    env:", 1
        )[0]
        self.assertIn("github.event.inputs.pr_number == ''", macos_condition)
        self.assertIn("inputs.pr_label == 'run-macos-ci'", macos_condition)

    def test_manual_pr_labels_route_only_their_selected_jobs(self):
        pek_ci = (REPO_ROOT / ".github/workflows/pek-ci.yml").read_text()
        jobs = (
            ("python-dependency-audit", "workflow-dependency-freshness", "run-python-audit"),
            ("workflow-dependency-freshness", "docker-scout", "run-workflow-audit"),
            ("docker-scout", "linux-quick-start-build-test", "run-docker-scout"),
        )
        for job, next_job, label in jobs:
            with self.subTest(job=job):
                job_text = pek_ci.split(f"  {job}:", 1)[1].split(
                    f"  {next_job}:", 1
                )[0]
                self.assertIn("inputs.checks == 'label'", job_text)
                self.assertIn(f"inputs.pr_label == '{label}'", job_text)

    def test_manual_pr_rpi_uses_the_validated_head_sha_without_host_python(self):
        pek_ci = (REPO_ROOT / ".github/workflows/pek-ci.yml").read_text()
        rpi_job = pek_ci.split("  rpi5-quick-start-build-test:", 1)[1].split(
            "  macos-nightly-test:", 1
        )[0]
        self.assertIn("github.event.inputs.pr_head_sha", rpi_job)
        self.assertNotIn("python3 scripts/private/github_pr_context.py", rpi_job)

    def test_agent_runtime_static_analysis_trigger_paths(self):
        quality_checks = load_quality_checks_module()

        for path in (
            "scripts/download-models.py",
            "scripts/private/agent_repair_orchestrator/cli.py",
            "scripts/private/agent_stabilization_orchestrator/cli.py",
            "scripts/private/agent_workflow_common/validation.py",
            "scripts/private/agent_runtime/openai_agent_runner.py",
            "scripts/private/github_actions.py",
            "scripts/private/tests/test_github_api.py",
            "scripts/private/test_support/agent_workflow.py",
            "tools/expkits-ci/expkits_ci/config_schema.py",
            "tools/expkits-ci/tests/test_agent_static_analysis.py",
        ):
            with self.subTest(path=path):
                self.assertTrue(
                    quality_checks.QualityChecks.should_run_agent_runtime_static_analysis(
                        [path]
                    )
                )

        self.assertFalse(
            quality_checks.QualityChecks.should_run_agent_runtime_static_analysis(
                ["scripts/private/unrelated_helper.py"]
            )
        )


if __name__ == "__main__":
    unittest.main()
