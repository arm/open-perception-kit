################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import json
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'scripts/private'))
from test_support.agent_workflow import (  # noqa: E402
    AGENT_MODEL_CONFIG_FILE,
    AGENT_REPAIR_SOURCE_RUN_WORKER_FILE,
    AGENT_REPAIR_SOURCE_RUN_WORKFLOW_FILE,
    AGENT_REVIEW_WORKFLOW_FILE,
    AGENT_STABILIZE_PR_LABEL_WORKFLOW_FILE,
    AGENT_STABILIZE_PR_WORKER_FILE,
    AGENT_TASK_CONFIG_FILE,
    OPENAI_AGENT_RUNNER_LABEL,
    PEK_CI_WORKFLOW_FILE,
    SOURCE_RUN_REPAIR_PROFILE_FILE,
    SONAR_WORKFLOW_FILE,
    STABILIZATION_PROFILE_FILE,
    WORKFLOW_AUDIT_FILE,
    WORKFLOW_DEPENDENCY_FRESHNESS_PROFILE_FILE,
    load_quality_checks_module,
    load_yaml,
    step_map,
)


class AgentWorkflowContractTests(unittest.TestCase):
    def assert_no_direct_model_flag(self, run: str) -> None:
        self.assertNotRegex(run, r"(^|\s)--model(\s|=|$)")

    def assert_no_direct_task_config_flags(self, run: str) -> None:
        for flag in (
            "--agent-instance",
            "--max-turns",
            "--max-prompt-chars",
            "--max-review-files",
            "--max-review-changed-lines",
        ):
            self.assertNotIn(flag, run)

    def test_manual_repair_wrapper_calls_repair_worker_with_minimal_inputs(self):
        workflow = load_yaml(AGENT_REPAIR_SOURCE_RUN_WORKFLOW_FILE)
        dispatch_inputs = workflow["on"]["workflow_dispatch"]["inputs"]
        repair_job = workflow["jobs"]["run-agent-repair-source-run"]

        self.assertEqual(
            set(dispatch_inputs.keys()),
            {
                "source_run_id",
                "target_branch",
                "task_ref",
                "profile_path",
                "dispatch_nonce",
            },
        )
        self.assertNotIn("workflow_run", workflow["on"])
        self.assertEqual(repair_job["uses"], "./.github/workflows/agent-repair-source-run-worker.yml")
        self.assertNotIn("run-agent-stabilizer", workflow["jobs"])
        self.assertEqual(dispatch_inputs["task_ref"]["default"], "")
        self.assertEqual(repair_job["with"]["task_ref"], "${{ inputs.task_ref || '' }}")
        self.assertEqual(
            set(repair_job["with"].keys()),
            {"source_run_id", "target_branch", "task_ref", "profile_path"},
        )
        self.assertNotIn("source_workflow_conclusion", repair_job["with"])
        self.assertNotIn("source_head_branch", repair_job["with"])
        self.assertNotIn("source_head_repository", repair_job["with"])
        self.assertEqual(repair_job["permissions"]["actions"], "write")
        self.assertEqual(repair_job["secrets"], "inherit")
        self.assertNotIn("inputs.pr_number", AGENT_REPAIR_SOURCE_RUN_WORKER_FILE.read_text(encoding="utf-8"))

    def test_reusable_workflow_uses_profile_and_direct_helper_commands(self):
        workflow = load_yaml(AGENT_REPAIR_SOURCE_RUN_WORKER_FILE)
        inputs = workflow["on"]["workflow_call"]["inputs"]
        prepare_job = workflow["jobs"]["prepare"]
        agent_job = workflow["jobs"]["agent-fix"]
        open_pr_job = workflow["jobs"]["open-pr"]
        prepare_steps = step_map(prepare_job)
        agent_steps = step_map(agent_job)
        open_pr_steps = step_map(open_pr_job)
        agent_step_names = [
            step.get("name") or step.get("id") or step.get("uses")
            for step in agent_job["steps"]
        ]

        self.assertEqual(set(workflow["jobs"]), {"prepare", "agent-fix", "open-pr"})
        self.assertEqual(
            set(inputs.keys()),
            {"source_run_id", "target_branch", "task_ref", "profile_path"},
        )
        self.assertEqual(
            inputs["profile_path"]["default"],
            ".github/agent-runtime/source-run-repair/profiles/profile.json",
        )
        self.assertEqual(inputs["task_ref"]["default"], "")

        output_steps = {
            "Resolve repair inputs": "resolve-inputs",
            "Package repository changes": "package-repository-changes",
            "Apply repair changes and push branch": "apply-repair-changes-and-push",
            "Create draft repair PR": "create-draft-pr",
        }
        all_steps = {**prepare_steps, **agent_steps, **open_pr_steps}
        for step_name, command in output_steps.items():
            run = all_steps[step_name]["run"]
            self.assertIn(f"python3 -m agent_repair_orchestrator {command}", run)
            self.assertIn('--github-output "${GITHUB_OUTPUT}"', run)
            self.assertIn('PYTHONPATH="${GITHUB_WORKSPACE}/scripts/private', run)

        apply_step_run = open_pr_steps["Apply repair changes and push branch"]["run"]
        self.assertEqual(
            open_pr_steps["Apply repair changes and push branch"]["env"]["GH_TOKEN"],
            "${{ secrets.EXPKITS_AGENT_TOKEN || github.token }}",
        )
        self.assertEqual(
            open_pr_steps["Create draft repair PR"]["env"]["GH_TOKEN"],
            "${{ secrets.EXPKITS_AGENT_TOKEN || github.token }}",
        )
        self.assertIn(
            '--target-branch "${{ needs.prepare.outputs.target_branch }}"',
            apply_step_run,
        )
        self.assertEqual(agent_job["runs-on"], OPENAI_AGENT_RUNNER_LABEL)
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
            agent_step_names.index("Fetch OpenAI proxy token"),
        )
        self.assertLess(
            agent_step_names.index("Fetch OpenAI proxy token"),
            agent_step_names.index("Run OpenAI SDK repair agent"),
        )
        self.assertLess(
            agent_step_names.index("Run OpenAI SDK repair agent"),
            agent_step_names.index("Package repository changes"),
        )
        python_step = agent_steps["Set up Agent Python"]
        self.assertEqual(python_step["uses"], "actions/setup-python@v6")
        self.assertEqual(python_step["with"]["python-version"], "3.10")
        install_step = agent_steps["Install OpenAI agent runtime"]
        self.assertEqual(install_step["shell"], "bash")
        self.assertIn("python3 scripts/private/agent_runtime/setup_runtime.py", install_step["run"])
        self.assertIn("--install-package ./tools/expkits-ci", install_step["run"])
        agent_step = agent_steps["Run OpenAI SDK repair agent"]
        self.assertEqual(agent_step["shell"], "bash")
        self.assertEqual(
            agent_step["env"]["OPENAI_PROXY_TOKEN"],
            "${{ steps.openai-token.outputs.openai_token }}",
        )
        self.assertIn(
            ".agent-runtime/openai-agent-venv/bin/python scripts/private/agent_runtime/openai_agent_runner.py run-repair",
            agent_step["run"],
        )
        self.assertIn(
            '--model-config-file "${{ needs.prepare.outputs.agent_model_config_file }}"',
            agent_step["run"],
        )
        self.assertIn(
            '--task-config-file "${{ needs.prepare.outputs.agent_task_config_file }}"',
            agent_step["run"],
        )
        self.assertIn("--prompt-file .agent-runtime/source-run-repair/goal.md", agent_step["run"])
        self.assertEqual(
            agent_step["run"].count("${{ runner.temp }}/agent-repair-source-run-agent-output.md"),
            1,
        )
        self.assert_no_direct_task_config_flags(agent_step["run"])
        self.assert_no_direct_model_flag(agent_step["run"])
        self.assertEqual(
            prepare_job["outputs"]["agent_model_config_file"],
            "${{ steps.resolve.outputs.agent_model_config_file }}",
        )
        self.assertEqual(
            prepare_job["outputs"]["agent_task_config_file"],
            "${{ steps.resolve.outputs.agent_task_config_file }}",
        )
        self.assertNotIn("agent_model", prepare_job["outputs"])
        static_regression_step = agent_steps["Run static regression tests"]
        self.assertIn(
            ".agent-runtime/openai-agent-venv/bin/python -m expkits_ci.agent_static_analysis",
            static_regression_step["run"],
        )
        self.assertIn(
            "python3 -m agent_repair_orchestrator run-validation",
            static_regression_step["run"],
        )
        self.assertIn(
            '${GITHUB_WORKSPACE}/scripts/private:${GITHUB_WORKSPACE}/tools/expkits-ci',
            static_regression_step["run"],
        )
        self.assertIn(
            "--profile-path \"${{ inputs.profile_path || '.github/agent-runtime/source-run-repair/profiles/profile.json' }}\"",
            static_regression_step["run"],
        )
        workflow_source = AGENT_REPAIR_SOURCE_RUN_WORKER_FILE.read_text(encoding="utf-8")
        self.assertNotIn("agent-stabilize-pr-worker.yml", workflow_source)
        self.assertNotIn("stabilize-pr", workflow_source)
        self.assertNotIn("merge-when-stable", workflow_source)
        self.assertNotIn("merge_pr", workflow_source)

    def test_agent_review_workflow_uses_openai_sdk_proxy_flow(self):
        workflow = load_yaml(AGENT_REVIEW_WORKFLOW_FILE)
        pull_request_trigger = workflow["on"]["pull_request"]
        dispatch_inputs = workflow["on"]["workflow_dispatch"]["inputs"]
        review_job = workflow["jobs"]["review"]
        review_gate_job = workflow["jobs"]["review-gate"]
        auto_stabilize_job = workflow["jobs"]["auto-stabilize-pr"]
        review_steps = step_map(review_job)

        self.assertNotIn("labeled", pull_request_trigger["types"])
        self.assertIn("edited", pull_request_trigger["types"])
        self.assertEqual(workflow["permissions"]["actions"], "read")
        self.assertEqual(workflow["permissions"]["contents"], "read")
        self.assertEqual(workflow["permissions"]["pull-requests"], "write")
        self.assertEqual(review_job["runs-on"], OPENAI_AGENT_RUNNER_LABEL)
        self.assertEqual(review_job["outputs"]["recommendation"], "${{ steps.render.outputs.recommendation }}")
        self.assertEqual(review_job["outputs"]["finding_count"], "${{ steps.render.outputs.finding_count }}")
        self.assertEqual(review_gate_job["needs"], "review")
        self.assertEqual(review_gate_job["runs-on"], "ubuntu-latest")
        self.assertEqual(review_gate_job["permissions"], {})
        self.assertIn("always()", review_gate_job["if"])
        self.assertNotIn("needs.review.result != 'skipped'", review_gate_job["if"])
        self.assertIn("needs.review.result != 'cancelled'", review_gate_job["if"])
        review_gate_steps = step_map(review_gate_job)
        gate_run = review_gate_steps["Require Agent Review approval"]["run"]
        gate_env = review_gate_steps["Require Agent Review approval"]["env"]
        self.assertEqual(gate_env["EVENT_NAME"], "${{ github.event_name }}")
        self.assertEqual(gate_env["HEAD_REPOSITORY"], "${{ github.event.pull_request.head.repo.full_name || '' }}")
        self.assertEqual(gate_env["REPOSITORY"], "${{ github.repository }}")
        self.assertIn('if [ "${REVIEW_RESULT}" = "skipped" ]; then', gate_run)
        self.assertIn('[ "${EVENT_NAME}" = "pull_request" ] && [ "${HEAD_REPOSITORY}" != "${REPOSITORY}" ]', gate_run)
        self.assertIn("unsupported fork pull request", gate_run)
        self.assertIn("the gate cannot pass without a review", gate_run)
        self.assertIn("Agent Review job was skipped unexpectedly.", gate_run)
        self.assertIn('if [ "${REVIEW_RESULT}" != "success" ]; then', gate_run)
        self.assertIn('if [ "${REVIEW_RECOMMENDATION}" != "approve" ]; then', gate_run)
        self.assertEqual(
            review_gate_steps["Require Agent Review approval"]["env"]["REVIEW_RECOMMENDATION"],
            "${{ needs.review.outputs.recommendation }}",
        )
        workflow_source = AGENT_REVIEW_WORKFLOW_FILE.read_text(encoding="utf-8")
        self.assertNotIn("agent-repair", workflow_source)
        self.assertNotIn("gpt-", workflow_source)
        self.assertNotIn("github.event.action", review_job["if"])
        self.assertNotIn("github.event.label", review_job["if"])
        self.assertEqual(auto_stabilize_job["needs"], "review")
        self.assertEqual(
            auto_stabilize_job["uses"],
            "./.github/workflows/agent-stabilize-pr-worker.yml",
        )
        self.assertIn("github.event_name == 'pull_request'", auto_stabilize_job["if"])
        self.assertIn("needs.review.result == 'success'", auto_stabilize_job["if"])
        self.assertIn("github.event.pull_request.head.repo.full_name == github.repository", auto_stabilize_job["if"])
        self.assertIn(
            "contains(github.event.pull_request.labels.*.name, 'agent-stabilize')",
            auto_stabilize_job["if"],
        )
        self.assertNotIn("github.event.action", auto_stabilize_job["if"])
        self.assertNotIn("github.event.label", auto_stabilize_job["if"])
        self.assertEqual(auto_stabilize_job["permissions"]["actions"], "read")
        self.assertEqual(auto_stabilize_job["permissions"]["contents"], "write")
        self.assertEqual(auto_stabilize_job["permissions"]["pull-requests"], "write")
        self.assertEqual(auto_stabilize_job["with"]["pr_number"], "${{ github.event.pull_request.number }}")
        self.assertEqual(auto_stabilize_job["with"]["head_sha"], "${{ github.event.pull_request.head.sha }}")
        self.assertEqual(auto_stabilize_job["with"]["source_run_id"], "${{ github.run_id }}")
        self.assertEqual(auto_stabilize_job["with"]["profile_path"],
                         ".github/agent-runtime/pr-stabilization/profiles/profile.json")
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
                "Build Agent review context",
                "Build Agent review packet",
                "Install OpenAI agent runtime",
                "Run Agent workflow static analysis",
                "Fetch OpenAI proxy token",
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
        self.assertIn("python3 scripts/private/agent_runtime/setup_runtime.py", install_step["run"])
        self.assertIn("--install-package ./tools/expkits-ci", install_step["run"])
        static_step = review_steps["Run Agent workflow static analysis"]
        self.assertEqual(static_step["shell"], "bash")
        self.assertIn(
            ".agent-runtime/openai-agent-venv/bin/python -m expkits_ci.agent_static_analysis --base-ref \"${REVIEW_BASE_REF}\"",
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
        context_step = review_steps["Build Agent review context"]
        self.assertEqual(context_step["env"]["REVIEW_HEAD_REF"], selected_head_ref)
        self.assertNotIn("REVIEW_PR_BODY", context_step["env"])
        self.assertIn(
            "python3 scripts/private/agent_runtime/review/context.py",
            context_step["run"],
        )
        self.assertIn(
            "--output .github/agent-runtime/review/out/review-context.json",
            context_step["run"],
        )
        self.assertIn('--repo-root "${GITHUB_WORKSPACE}"', context_step["run"])
        self.assertNotIn("review.prompt.md", context_step["run"])
        agent_step = review_steps["Run OpenAI SDK review"]
        self.assertEqual(agent_step["shell"], "bash")
        self.assertEqual(
            agent_step["env"]["OPENAI_PROXY_TOKEN"],
            "${{ steps.openai-token.outputs.openai_token }}",
        )
        self.assertEqual(
            agent_step["env"]["AGENT_ACTION_LOG"],
            ".github/agent-runtime/review/out/agent-actions.jsonl",
        )
        self.assertIn(
            ".agent-runtime/openai-agent-venv/bin/python scripts/private/agent_runtime/openai_agent_runner.py run-review",
            agent_step["run"],
        )
        self.assertEqual(
            agent_step["run"].count("--model-config-file .github/agent-runtime/runtime/agent-models.json"),
            1,
        )
        self.assertNotIn("--schema-file", agent_step["run"])
        self.assertEqual(
            agent_step["run"].count("--output-file .github/agent-runtime/review/out/review.json"),
            1,
        )
        self.assertEqual(
            agent_step["run"].count("--context-file .github/agent-runtime/review/out/review-context.json"),
            1,
        )
        self.assertNotIn("--prompt-file", agent_step["run"])
        self.assertEqual(
            agent_step["run"].count("--task-config-file .github/agent-runtime/runtime/agent-tasks.json"),
            1,
        )
        self.assert_no_direct_task_config_flags(agent_step["run"])
        self.assert_no_direct_model_flag(agent_step["run"])
        upload_step = review_steps["Upload review artifacts"]
        self.assertEqual(upload_step["with"]["path"], ".github/agent-runtime/review/out")
        publish_step = review_steps["Publish review summary comment"]
        render_summary_step = review_steps["Render review summary"]
        self.assertEqual(render_summary_step["id"], "render")
        self.assertEqual(
            render_summary_step["env"]["REVIEW_BASE_REF"],
            "${{ github.event_name == 'workflow_dispatch' && github.event.inputs.base_ref || format('origin/{0}', github.base_ref) }}",
        )
        self.assertIn('--github-output "${GITHUB_OUTPUT}"', render_summary_step["run"])
        self.assertEqual(
            publish_step["env"]["REVIEW_BASE_REF"],
            "${{ format('origin/{0}', github.base_ref) }}",
        )
        self.assertIn(
            ".agent-runtime/openai-agent-venv/bin/python scripts/private/agent_runtime/review/publish.py",
            publish_step["run"],
        )

    def test_agent_stabilize_label_workflow_is_separate_from_review_gate(self):
        workflow = load_yaml(AGENT_STABILIZE_PR_LABEL_WORKFLOW_FILE)
        job = workflow["jobs"]["run-agent-stabilizer"]

        self.assertEqual(workflow["on"]["pull_request"]["types"], ["labeled"])
        self.assertEqual(workflow["permissions"], {})
        self.assertEqual(set(workflow["jobs"]), {"run-agent-stabilizer"})
        self.assertNotIn("review-gate", workflow["jobs"])
        self.assertIn("github.event.pull_request.head.repo.full_name == github.repository", job["if"])
        self.assertIn("github.event.label.name == 'agent-stabilize'", job["if"])
        self.assertEqual(job["uses"], "./.github/workflows/agent-stabilize-pr-worker.yml")
        self.assertEqual(job["permissions"]["actions"], "read")
        self.assertEqual(job["permissions"]["contents"], "write")
        self.assertEqual(job["permissions"]["pull-requests"], "write")
        self.assertEqual(job["with"]["pr_number"], "${{ github.event.pull_request.number }}")
        self.assertEqual(job["with"]["head_sha"], "${{ github.event.pull_request.head.sha }}")
        self.assertEqual(job["with"]["task_ref"], "${{ github.event.pull_request.title }}")
        self.assertEqual(
            job["with"]["profile_path"],
            ".github/agent-runtime/pr-stabilization/profiles/profile.json",
        )
        self.assertNotIn("source_run_id", job["with"])
        self.assertEqual(job["secrets"], "inherit")

    def test_agent_runtime_static_analysis_triggers_for_new_helper_package_files(self):
        quality_checks = load_quality_checks_module()

        self.assertTrue(
            quality_checks.QualityChecks.should_run_agent_runtime_static_analysis(
                ["scripts/private/agent_repair_orchestrator/cli.py"],
            )
        )
        self.assertTrue(
            quality_checks.QualityChecks.should_run_agent_runtime_static_analysis(
                ["scripts/private/agent_stabilization_orchestrator/cli.py"],
            )
        )
        self.assertTrue(
            quality_checks.QualityChecks.should_run_agent_runtime_static_analysis(
                ["scripts/private/agent_workflow_common/validation.py"],
            )
        )
        self.assertTrue(
            quality_checks.QualityChecks.should_run_agent_runtime_static_analysis(
                ["scripts/private/agent_runtime/openai_agent_runner.py"],
            )
        )
        self.assertTrue(
            quality_checks.QualityChecks.should_run_agent_runtime_static_analysis(
                ["scripts/private/github_actions.py"],
            )
        )
        self.assertTrue(
            quality_checks.QualityChecks.should_run_agent_runtime_static_analysis(
                ["scripts/private/tests/test_github_api.py"],
            )
        )
        self.assertTrue(
            quality_checks.QualityChecks.should_run_agent_runtime_static_analysis(
                ["scripts/private/test_support/agent_workflow.py"],
            )
        )
        self.assertTrue(
            quality_checks.QualityChecks.should_run_agent_runtime_static_analysis(
                ["tools/expkits-ci/tests/test_agent_static_analysis.py"],
            )
        )
        self.assertFalse(
            quality_checks.QualityChecks.should_run_agent_runtime_static_analysis(
                ["scripts/private/unrelated_helper.py"],
            )
        )

    def test_standard_validation_workflows_accept_manual_pr_context(self):
        pek_ci = load_yaml(PEK_CI_WORKFLOW_FILE)
        sonar = load_yaml(SONAR_WORKFLOW_FILE)
        pek_inputs = pek_ci["on"]["workflow_dispatch"]["inputs"]
        sonar_inputs = sonar["on"]["workflow_dispatch"]["inputs"]
        pek_steps = step_map(pek_ci["jobs"]["quality-checks"])
        sonar_steps = step_map(sonar["jobs"]["build-and-sonar"])
        linux_steps = step_map(pek_ci["jobs"]["linux-quick-start-build-test"])
        rpi_steps = step_map(pek_ci["jobs"]["rpi5-quick-start-build-test"])
        expected_label_gate = "github.event.action != 'labeled' || contains(github.event.label.name, 'run-pek-ci')"
        expected_draft_override = (
            "github.event.action == 'labeled' && contains(github.event.label.name, 'run-pek-ci')"
        )

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
        self.assertIn("Checkout workflow helpers", pek_steps)
        self.assertIn("Checkout workflow helpers", sonar_steps)
        for steps in (linux_steps, rpi_steps, pek_steps, sonar_steps):
            self.assertIn("Resolve manual PR context", steps)
            self.assertIn("Checkout workflow helpers", steps)
            resolver_run = steps["Resolve manual PR context"]["run"]
            self.assertIn("python3 scripts/private/github_pr_context.py", resolver_run)
            self.assertIn('--pr-number "${{ github.event.inputs.pr_number }}"', resolver_run)
            self.assertIn('--base-ref-override "${{ github.event.inputs.pr_base_ref }}"', resolver_run)
            self.assertIn('--head-ref-override "${{ github.event.inputs.pr_head_ref }}"', resolver_run)
            self.assertIn('--head-sha-override "${{ github.event.inputs.pr_head_sha }}"', resolver_run)
            self.assertIn('--github-output "${GITHUB_OUTPUT}"', resolver_run)
            self.assertNotIn("gh pr view", resolver_run)
            self.assertEqual(
                steps["Checkout workflow helpers"]["with"]["persist-credentials"],
                "false",
            )
        for job_name in ("linux-quick-start-build-test", "rpi5-quick-start-build-test", "quality-checks"):
            job_condition = pek_ci["jobs"][job_name]["if"]
            self.assertIn(expected_label_gate, job_condition)
            self.assertIn(expected_draft_override, job_condition)
        sonar_condition = sonar["jobs"]["build-and-sonar"]["if"]
        self.assertIn(expected_label_gate, sonar_condition)
        self.assertIn(expected_draft_override, sonar_condition)
        for steps in (linux_steps, rpi_steps, pek_steps, sonar_steps):
            checkout_ref = steps["Checkout"]["with"]["ref"]
            self.assertIn("steps.manual_pr.outputs.head_sha", checkout_ref)
            self.assertIn("steps.manual_pr.outputs.head_ref", checkout_ref)
            self.assertIn("github.head_ref", checkout_ref)
            self.assertNotIn("github.event.inputs.pr_head_sha", checkout_ref)
            self.assertNotIn("github.event.inputs.pr_head_ref", checkout_ref)
        self.assertIn("steps.manual_pr.outputs.head_sha", pek_steps["Checkout"]["with"]["ref"])
        self.assertIn("steps.manual_pr.outputs.head_sha", sonar_steps["Checkout"]["with"]["ref"])
        self.assertIn(
            "steps.manual_pr.outputs.base_ref",
            pek_steps["Check Repo Quality gate (PR)"]["run"],
        )
        self.assertIn(
            "-e PULL_REQUEST_TARGET_BRANCH",
            pek_steps["Check Repo Quality gate (PR)"]["run"],
        )
        self.assertIn(
            "source scripts/private/ci_git_auth_env.sh",
            pek_steps["Check Repo Quality gate (PR)"]["run"],
        )
        self.assertIn(
            "source scripts/private/ci_git_auth_env.sh",
            pek_steps["Run clang-tidy baseline check"]["run"],
        )
        self.assertIn(
            "github.event.inputs.pr_number",
            pek_steps["Run clang-tidy baseline check"]["env"]["PR_CONTEXT_RUN"],
        )
        self.assertNotIn("${{ inputs.", PEK_CI_WORKFLOW_FILE.read_text(encoding="utf-8"))
        self.assertNotIn("${{ inputs.", SONAR_WORKFLOW_FILE.read_text(encoding="utf-8"))
        self.assertNotIn("gh pr view", PEK_CI_WORKFLOW_FILE.read_text(encoding="utf-8"))
        self.assertNotIn("gh pr view", SONAR_WORKFLOW_FILE.read_text(encoding="utf-8"))
        self.assertIn("steps.manual_pr.outputs.pr_number", sonar_steps["SonarQube analysis"]["env"]["PR_KEY"])
        self.assertIn("steps.manual_pr.outputs.head_ref", sonar_steps["SonarQube analysis"]["env"]["SONAR_BRANCH"])
        self.assertIn("steps.manual_pr.outputs.base_ref", sonar_steps["SonarQube analysis"]["env"]["PR_BASE"])
        self.assertIn(
            "python3 scripts/private/sonar_quality_gate_workflow.py probe-api-access",
            sonar_steps["Probe Sonar API access"]["run"],
        )
        self.assertIn(
            "python3 scripts/private/sonar_quality_gate_workflow.py report-quality-gate",
            sonar_steps["Report Sonar quality gate details"]["run"],
        )
        rpi_job = pek_ci["jobs"]["rpi5-quick-start-build-test"]
        sonar_job = sonar["jobs"]["build-and-sonar"]
        self.assertEqual(
            rpi_steps["Checkout workflow helpers"]["with"]["path"],
            "${{ env.CI_HELPER_PATH }}",
        )
        self.assertEqual(
            sonar_steps["Checkout workflow helpers"]["with"]["path"],
            "${{ env.CI_HELPER_PATH }}",
        )
        self.assertIn(
            '"${{ github.workspace }}/${{ env.CI_HELPER_PATH }}"',
            rpi_steps["Clean quick-start workspace"]["run"],
        )
        self.assertIn(
            '"${GITHUB_WORKSPACE}/${CI_HELPER_PATH}"',
            sonar_steps["Cleanup isolated workspace"]["run"],
        )
        self.assertIn("CI_HELPER_PATH", rpi_job["env"])
        self.assertIn("CI_HELPER_PATH", sonar_job["env"])

    def test_modified_validation_workflows_use_canonical_artifact_upload_major(self):
        workflows = {
            "agent-review": load_yaml(AGENT_REVIEW_WORKFLOW_FILE),
            "agent-stabilize-pr-on-label": load_yaml(AGENT_STABILIZE_PR_LABEL_WORKFLOW_FILE),
            "agent-stabilize-pr-worker": load_yaml(AGENT_STABILIZE_PR_WORKER_FILE),
            "pek-ci": load_yaml(PEK_CI_WORKFLOW_FILE),
            "sonar": load_yaml(SONAR_WORKFLOW_FILE),
            "agent-repair-source-run-worker": load_yaml(AGENT_REPAIR_SOURCE_RUN_WORKER_FILE),
            "workflow-audit": load_yaml(WORKFLOW_AUDIT_FILE),
            "agent-repair-source-run": load_yaml(AGENT_REPAIR_SOURCE_RUN_WORKFLOW_FILE),
        }

        for workflow_name, workflow in workflows.items():
            for job in workflow["jobs"].values():
                for step in job.get("steps", []):
                    with self.subTest(workflow=workflow_name, step=step.get("name")):
                        if step.get("uses", "").startswith("actions/upload-artifact@"):
                            self.assertEqual(step["uses"], "actions/upload-artifact@v6")

    def test_stabilizer_workflow_uses_canonical_agent_review_shape(self):
        workflow = load_yaml(AGENT_STABILIZE_PR_WORKER_FILE)
        call_inputs = workflow["on"]["workflow_call"]["inputs"]
        job = workflow["jobs"]["stabilize"]
        steps = step_map(job)

        self.assertNotIn("workflow_dispatch", workflow["on"])
        self.assertIn("dispatch_nonce", call_inputs)
        self.assertEqual(job["runs-on"], OPENAI_AGENT_RUNNER_LABEL)
        self.assertEqual(job["permissions"]["actions"], "read")
        self.assertEqual(steps["Checkout workflow helpers"]["with"]["ref"], "${{ steps.helper_ref.outputs.head_sha }}")
        self.assertEqual(steps["Checkout workflow helpers"]["with"]["persist-credentials"], "false")
        self.assertEqual(steps["Checkout PR head"]["with"]["persist-credentials"], "false")
        self.assertEqual(
            list(steps),
            [
                "Resolve helper checkout ref",
                "Checkout workflow helpers",
                "Snapshot workflow helper bundle",
                "Resolve PR details",
                "Checkout PR head",
                "Restore workflow helper bundle",
                "Prepare stabilization context",
                "Set up Agent Python",
                "Install OpenAI agent runtime",
                "Fetch OpenAI proxy token",
                "Run OpenAI SDK stabilization agent",
                "Run stabilization validation",
                "Commit stabilization fix",
                "Write stabilization skip artifact",
                "Upload stabilization artifacts",
            ],
        )
        helper_ref_step = steps["Resolve helper checkout ref"]
        self.assertIn("inputs.head_sha", helper_ref_step["run"])
        self.assertIn("gh pr view", helper_ref_step["run"])
        self.assertIn("headRefOid", helper_ref_step["run"])
        snapshot_step = steps["Snapshot workflow helper bundle"]
        self.assertIn("python3 -m agent_stabilization_orchestrator snapshot-helper-bundle", snapshot_step["run"])
        self.assertIn('--bundle-root "${RUNNER_TEMP}/agent-stabilization-helper"', snapshot_step["run"])
        python_step = steps["Set up Agent Python"]
        self.assertEqual(python_step["if"], "${{ steps.context.outputs.review_recommendation != 'approve' }}")
        self.assertEqual(python_step["uses"], "actions/setup-python@v6")
        self.assertEqual(python_step["with"]["python-version"], "3.10")
        install_step = steps["Install OpenAI agent runtime"]
        self.assertEqual(install_step["shell"], "bash")
        self.assertIn(
            "python3 .agent-runtime/agent-stabilization-helper/scripts/private/agent_runtime/setup_runtime.py",
            install_step["run"],
        )
        self.assertIn(
            "--requirements-file .agent-runtime/agent-stabilization-helper/.github/agent-runtime/runtime/requirements-openai-agents.txt",
            install_step["run"],
        )
        agent_step = steps["Run OpenAI SDK stabilization agent"]
        self.assertEqual(agent_step["shell"], "bash")
        self.assertEqual(
            agent_step["env"]["OPENAI_PROXY_TOKEN"],
            "${{ steps.openai-token.outputs.openai_token }}",
        )
        self.assertIn(
            ".agent-runtime/openai-agent-venv/bin/python .agent-runtime/agent-stabilization-helper/scripts/private/agent_runtime/openai_agent_runner.py run-stabilization",
            agent_step["run"],
        )
        self.assertIn(
            '--model-config-file "${{ steps.context.outputs.agent_model_config_file }}"',
            agent_step["run"],
        )
        self.assertIn(
            '--task-config-file "${{ steps.context.outputs.agent_task_config_file }}"',
            agent_step["run"],
        )
        self.assertIn('--prompt-file "${{ inputs.context_root }}/stabilize-goal.md"', agent_step["run"])
        self.assertIn(
            '--output-file "${{ runner.temp }}/agent-stabilize-pr-output.md"',
            agent_step["run"],
        )
        self.assert_no_direct_task_config_flags(agent_step["run"])
        self.assert_no_direct_model_flag(agent_step["run"])
        resolve_pr_step = steps["Resolve PR details"]
        self.assertEqual(resolve_pr_step["shell"], "bash")
        self.assertIn("python3 -m agent_stabilization_orchestrator resolve-pr-details", resolve_pr_step["run"])
        self.assertIn('--pr-number "${{ inputs.pr_number }}"', resolve_pr_step["run"])
        self.assertIn('--github-output "${GITHUB_OUTPUT}"', resolve_pr_step["run"])
        self.assertEqual(snapshot_step["shell"], "bash")
        restore_step = steps["Restore workflow helper bundle"]
        self.assertEqual(restore_step["shell"], "bash")
        self.assertIn("restore-helper-bundle", restore_step["run"])
        self.assertIn('--helper-root ".agent-runtime/agent-stabilization-helper"', restore_step["run"])
        context_step = steps["Prepare stabilization context"]
        self.assertEqual(context_step["shell"], "bash")
        self.assertNotIn("uses", context_step)
        self.assertIn("python3 -m agent_stabilization_orchestrator prepare-stabilization-context", context_step["run"])
        self.assertIn(
            'PYTHONPATH="${GITHUB_WORKSPACE}/.agent-runtime/agent-stabilization-helper/scripts/private',
            context_step["run"],
        )
        self.assertIn(
            '--profile-path ".agent-runtime/agent-stabilization-helper/${{ inputs.profile_path }}"',
            context_step["run"],
        )
        self.assertIn('--github-output "${GITHUB_OUTPUT}"', context_step["run"])
        validation_step = steps["Run stabilization validation"]
        self.assertEqual(validation_step["shell"], "bash")
        self.assertNotIn("uses", validation_step)
        self.assertEqual(validation_step["env"]["GH_TOKEN"], "")
        self.assertEqual(validation_step["env"]["GITHUB_TOKEN"], "")
        self.assertEqual(validation_step["env"]["OPENAI_PROXY_TOKEN"], "")
        self.assertEqual(validation_step["env"]["OPENAI_API_KEY"], "")
        self.assertIn(
            "python3 -m agent_stabilization_orchestrator run-validation",
            validation_step["run"],
        )
        self.assertIn(
            '--profile-path "${RUNNER_TEMP}/agent-stabilization-helper/${{ inputs.profile_path }}"',
            validation_step["run"],
        )
        commit_step = steps["Commit stabilization fix"]
        self.assertEqual(commit_step["shell"], "bash")
        self.assertNotIn("uses", commit_step)
        self.assertIn(
            "python3 -m agent_stabilization_orchestrator commit-review-fix",
            commit_step["run"],
        )
        self.assertIn('--context-root "${{ inputs.context_root }}"', commit_step["run"])
        self.assertIn('--pr-number "${{ inputs.pr_number }}"', commit_step["run"])
        self.assertIn('--head-branch "${{ steps.pr.outputs.head_branch }}"', commit_step["run"])
        skip_step = steps["Write stabilization skip artifact"]
        self.assertEqual(skip_step["if"], "${{ steps.context.outputs.review_recommendation == 'approve' }}")
        self.assertIn('mkdir -p "${{ runner.temp }}"', skip_step["run"])
        self.assertIn("agent-stabilize-pr-output.md", skip_step["run"])
        self.assertIn("No stabilization agent run was needed", skip_step["run"])
        self.assertEqual(
            commit_step["env"]["GH_TOKEN"],
            "${{ secrets.EXPKITS_AGENT_TOKEN }}",
        )

    def test_agent_runtime_config_contracts_are_centralized(self):
        model_config = json.loads(AGENT_MODEL_CONFIG_FILE.read_text(encoding="utf-8"))
        task_config = json.loads(AGENT_TASK_CONFIG_FILE.read_text(encoding="utf-8"))
        workflows = {
            "agent-review": AGENT_REVIEW_WORKFLOW_FILE,
            "agent-stabilize-pr-worker": AGENT_STABILIZE_PR_WORKER_FILE,
            "agent-repair-source-run-worker": AGENT_REPAIR_SOURCE_RUN_WORKER_FILE,
        }

        self.assertEqual(set(model_config["agents"]), {"review", "repair", "stabilization"})
        self.assertIsInstance(model_config["default_agent_model"], str)
        self.assertTrue(model_config["default_agent_model"].strip())
        for agent_name, agent_config in model_config["agents"].items():
            self.assertEqual(set(agent_config), {"model"}, agent_name)
            self.assertIsInstance(agent_config["model"], str)
            self.assertTrue(agent_config["model"].strip(), agent_name)

        for profile_file in (
            SOURCE_RUN_REPAIR_PROFILE_FILE,
            WORKFLOW_DEPENDENCY_FRESHNESS_PROFILE_FILE,
            STABILIZATION_PROFILE_FILE,
        ):
            profile = json.loads(profile_file.read_text(encoding="utf-8"))
            with self.subTest(profile=profile_file.name):
                self.assertNotIn("agent_model", profile)
                self.assertEqual(
                    profile["agent_model_config"],
                    ".github/agent-runtime/runtime/agent-models.json",
                )
                self.assertEqual(
                    profile["agent_task_config"],
                    ".github/agent-runtime/runtime/agent-tasks.json",
                )

        self.assertEqual(set(task_config["tasks"]), {"run-review", "run-repair", "run-stabilization"})
        for command, settings in task_config["tasks"].items():
            with self.subTest(command=command):
                expected_common = {"agent_instance", "max_turns"}
                if command == "run-review":
                    self.assertEqual(
                        set(settings),
                        expected_common | {"max_review_files", "max_review_changed_lines"},
                    )
                    self.assertNotIn("max_prompt_chars", settings)
                else:
                    self.assertEqual(set(settings), expected_common | {"max_prompt_chars"})
                    self.assertIsInstance(settings["max_prompt_chars"], int)
                    self.assertGreater(settings["max_prompt_chars"], 0)
                self.assertIsInstance(settings["agent_instance"], str)
                self.assertIsInstance(settings["max_turns"], int)
                self.assertGreater(settings["max_turns"], 0)

        for workflow_name, workflow_file in workflows.items():
            workflow_source = workflow_file.read_text(encoding="utf-8")
            with self.subTest(workflow=workflow_name):
                self.assertNotIn("gpt-", workflow_source)
                self.assertNotRegex(workflow_source, r"(^|\s)--model(\s|=|$)")
                self.assert_no_direct_task_config_flags(workflow_source)
                self.assertEqual(
                    workflow_source.count("--model-config-file"),
                    workflow_source.count("openai_agent_runner.py "),
                )
                expected_task_config_uses = workflow_source.count("openai_agent_runner.py ")
                if workflow_name == "agent-review":
                    expected_task_config_uses += 1
                self.assertEqual(
                    workflow_source.count("--task-config-file"),
                    expected_task_config_uses,
                )

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
        self.assertIn("scripts/private/github_api.py", pull_request_paths)
        self.assertIn("--summary-limit", report_steps["Render workflow dependency freshness report"]["run"])
        self.assertNotIn("--github-output", report_steps["Render workflow dependency freshness report"]["run"])


if __name__ == "__main__":
    unittest.main()
