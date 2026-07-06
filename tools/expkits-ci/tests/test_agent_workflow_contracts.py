################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'scripts/private/tests'))
from agent_workflow_test_support import (  # noqa: E402
    AGENT_REVIEW_WORKFLOW_FILE,
    HELPER_RUNTIME,
    OPENAI_AGENT_RUNNER_LABEL,
    PEK_CI_WORKFLOW_FILE,
    REUSABLE_WORKFLOW_FILE,
    SONAR_WORKFLOW_FILE,
    STABILIZER_WORKFLOW_FILE,
    WORKFLOW_AUDIT_FILE,
    WORKFLOW_FILE,
    load_quality_checks_module,
    load_yaml,
    step_map,
)


class AgentWorkflowContractTests(unittest.TestCase):
    def test_manual_wrapper_calls_reusable_workflow_with_minimal_inputs(self):
        workflow = load_yaml(WORKFLOW_FILE)
        dispatch_inputs = workflow["on"]["workflow_dispatch"]["inputs"]
        repair_job = workflow["jobs"]["run-workflow-action-update-agent"]
        stabilize_job = workflow["jobs"]["run-agent-stabilizer"]

        self.assertEqual(
            set(dispatch_inputs.keys()),
            {
                "source_run_id",
                "pr_number",
                "head_sha",
                "target_branch",
                "task_ref",
                "profile_path",
                "context_root",
                "dispatch_nonce",
            },
        )
        self.assertNotIn("workflow_run", workflow["on"])
        self.assertEqual(repair_job["uses"], "./.github/workflows/workflow-action-update-agent-reusable.yml")
        self.assertEqual(stabilize_job["uses"], "./.github/workflows/agent-stabilize-pr.yml")
        self.assertEqual(repair_job["if"], "${{ inputs.pr_number == '' }}")
        self.assertEqual(stabilize_job["if"], "${{ inputs.pr_number != '' }}")
        self.assertEqual(dispatch_inputs["task_ref"]["default"], "")
        self.assertEqual(repair_job["with"]["task_ref"], "${{ inputs.task_ref || '' }}")
        self.assertEqual(stabilize_job["with"]["task_ref"], "${{ inputs.task_ref || '' }}")
        self.assertEqual(
            set(repair_job["with"].keys()),
            {"source_run_id", "target_branch", "task_ref", "profile_path"},
        )
        self.assertEqual(
            set(stabilize_job["with"].keys()),
            {"pr_number", "head_sha", "source_run_id", "task_ref", "profile_path", "context_root", "dispatch_nonce"},
        )
        self.assertNotIn("source_workflow_conclusion", repair_job["with"])
        self.assertNotIn("source_head_branch", repair_job["with"])
        self.assertNotIn("source_head_repository", repair_job["with"])
        self.assertEqual(repair_job["permissions"]["actions"], "write")
        self.assertEqual(stabilize_job["permissions"]["actions"], "read")
        self.assertEqual(repair_job["secrets"], "inherit")
        self.assertEqual(stabilize_job["secrets"], "inherit")

    def test_reusable_workflow_uses_profile_and_direct_helper_commands(self):
        workflow = load_yaml(REUSABLE_WORKFLOW_FILE)
        inputs = workflow["on"]["workflow_call"]["inputs"]
        prepare_job = workflow["jobs"]["prepare"]
        agent_job = workflow["jobs"]["agent-fix"]
        open_pr_job = workflow["jobs"]["open-pr"]
        stabilize_job = workflow["jobs"]["stabilize-pr"]
        prepare_steps = step_map(prepare_job)
        agent_steps = step_map(agent_job)
        open_pr_steps = step_map(open_pr_job)
        stabilize_steps = step_map(stabilize_job)
        agent_step_names = [
            step.get("name") or step.get("id") or step.get("uses")
            for step in agent_job["steps"]
        ]

        self.assertEqual(
            set(inputs.keys()),
            {"source_run_id", "target_branch", "task_ref", "profile_path"},
        )
        self.assertEqual(
            inputs["profile_path"]["default"],
            ".github/agent-runtime/workflow-action-update-agent/profiles/profile.json",
        )
        self.assertEqual(inputs["task_ref"]["default"], "")

        output_steps = {
            "Resolve repair inputs": "resolve-inputs",
            "Package repository changes": "package-repository-changes",
            "Apply repair changes and push branch": "apply-repair-changes-and-push",
            "Create draft repair PR": "create-draft-pr",
        }
        all_steps = {**prepare_steps, **agent_steps, **open_pr_steps, **stabilize_steps}
        for step_name, command in output_steps.items():
            run = all_steps[step_name]["run"]
            self.assertIn(f"python3 -m workflow_action_update_agent {command}", run)
            self.assertIn('--github-output "${GITHUB_OUTPUT}"', run)
            self.assertIn('PYTHONPATH="${GITHUB_WORKSPACE}/scripts/private', run)

        apply_step_run = open_pr_steps["Apply repair changes and push branch"]["run"]
        self.assertIn(
            '--target-branch "${{ needs.prepare.outputs.target_branch }}"',
            apply_step_run,
        )
        self.assertEqual(agent_job["runs-on"], OPENAI_AGENT_RUNNER_LABEL)
        self.assertEqual(stabilize_job["runs-on"], "ubuntu-latest")
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
            ".agent-runtime/openai-agent-venv/bin/python -m expkits_ci.agent_static_analysis",
            static_regression_step["run"],
        )
        self.assertIn(
            "python3 -m workflow_action_update_agent run-validation",
            static_regression_step["run"],
        )
        self.assertIn(
            '${GITHUB_WORKSPACE}/scripts/private:${GITHUB_WORKSPACE}/tools/expkits-ci',
            static_regression_step["run"],
        )
        self.assertIn(
            "--profile-path \"${{ inputs.profile_path || '.github/agent-runtime/workflow-action-update-agent/profiles/profile.json' }}\"",
            static_regression_step["run"],
        )
        self.assertEqual(stabilize_job["permissions"]["actions"], "write")
        self.assertEqual(stabilize_steps["Checkout workflow helpers"]["uses"], "actions/checkout@v6")
        stabilize_run = stabilize_steps["Stabilize repair PR"]["run"]
        self.assertEqual(
            stabilize_steps["Stabilize repair PR"]["shell"],
            "bash",
        )
        self.assertEqual(
            stabilize_steps["Stabilize repair PR"]["env"]["GITHUB_TOKEN"],
            "${{ github.token }}",
        )
        self.assertEqual(
            stabilize_steps["Stabilize repair PR"]["env"]["GH_TOKEN"],
            "${{ secrets.EXPKITS_AGENT_TOKEN || github.token }}",
        )
        self.assertIn("python3 -m workflow_action_update_agent stabilize-pr", stabilize_run)
        self.assertIn("--merge-when-stable", stabilize_run)

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
        self.assertTrue(
            quality_checks.QualityChecks.should_run_agent_runtime_static_analysis(
                ["scripts/private/tests/test_github_api.py"],
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
        linux_checkout = pek_ci["jobs"]["linux-quick-start-build-test"]["steps"][0]
        rpi_checkout = pek_ci["jobs"]["rpi5-quick-start-build-test"]["steps"][0]
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
        self.assertEqual(
            set(HELPER_RUNTIME.canonical_validation_workflow("pek-ci")["workflow_dispatch_inputs"].keys()),
            set(pek_inputs.keys()),
        )
        self.assertEqual(
            set(HELPER_RUNTIME.canonical_validation_workflow("sonar")["workflow_dispatch_inputs"].keys()),
            set(sonar_inputs.keys()),
        )
        self.assertIn("Resolve manual PR context", pek_steps)
        self.assertIn("Resolve manual PR context", sonar_steps)
        self.assertIn("Checkout workflow helpers", pek_steps)
        self.assertIn("Checkout workflow helpers", sonar_steps)
        for steps in (pek_steps, sonar_steps):
            resolver_run = steps["Resolve manual PR context"]["run"]
            self.assertIn("python3 scripts/private/github_pr_context.py", resolver_run)
            self.assertIn('--pr-number "${{ github.event.inputs.pr_number }}"', resolver_run)
            self.assertIn('--base-ref-override "${{ github.event.inputs.pr_base_ref }}"', resolver_run)
            self.assertIn('--head-ref-override "${{ github.event.inputs.pr_head_ref }}"', resolver_run)
            self.assertIn('--head-sha-override "${{ github.event.inputs.pr_head_sha }}"', resolver_run)
            self.assertIn('--github-output "${GITHUB_OUTPUT}"', resolver_run)
            self.assertNotIn("gh pr view", resolver_run)
        for job_name in ("linux-quick-start-build-test", "rpi5-quick-start-build-test", "quality-checks"):
            job_condition = pek_ci["jobs"][job_name]["if"]
            self.assertIn(expected_label_gate, job_condition)
            self.assertIn(expected_draft_override, job_condition)
        sonar_condition = sonar["jobs"]["build-and-sonar"]["if"]
        self.assertIn(expected_label_gate, sonar_condition)
        self.assertIn(expected_draft_override, sonar_condition)
        for checkout_step in (linux_checkout, rpi_checkout):
            checkout_ref = checkout_step["with"]["ref"]
            self.assertIn("github.event_name == 'workflow_dispatch'", checkout_ref)
            self.assertIn("github.event.inputs.pr_head_sha", checkout_ref)
            self.assertIn("github.event.inputs.pr_head_ref", checkout_ref)
            self.assertIn("github.head_ref", checkout_ref)
        self.assertIn("steps.manual_pr.outputs.head_sha", pek_steps["Checkout"]["with"]["ref"])
        self.assertNotIn("github.event.inputs.pr_head_sha", pek_steps["Checkout"]["with"]["ref"])
        self.assertIn("steps.manual_pr.outputs.head_sha", sonar_steps["Checkout"]["with"]["ref"])
        self.assertNotIn("github.event.inputs.pr_head_sha", sonar_steps["Checkout"]["with"]["ref"])
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

    def test_stabilizer_workflow_uses_canonical_agent_review_shape(self):
        workflow = load_yaml(STABILIZER_WORKFLOW_FILE)
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
        self.assertIn("python3 -m workflow_action_update_agent snapshot-helper-bundle", snapshot_step["run"])
        self.assertIn('--bundle-root "${RUNNER_TEMP}/workflow-action-update-agent-helper"', snapshot_step["run"])
        python_step = steps["Set up Agent Python"]
        self.assertEqual(python_step["if"], "${{ steps.context.outputs.review_recommendation != 'approve' }}")
        self.assertEqual(python_step["uses"], "actions/setup-python@v6")
        self.assertEqual(python_step["with"]["python-version"], "3.10")
        install_step = steps["Install OpenAI agent runtime"]
        self.assertEqual(install_step["shell"], "bash")
        self.assertIn(
            "python3 .workflow-action-update-agent-helper/scripts/private/agent_runtime/setup_runtime.py",
            install_step["run"],
        )
        self.assertIn(
            "--requirements-file .workflow-action-update-agent-helper/.github/agent-runtime/runtime/requirements-openai-agents.txt",
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
        resolve_pr_step = steps["Resolve PR details"]
        self.assertEqual(resolve_pr_step["shell"], "bash")
        self.assertIn("python3 -m workflow_action_update_agent resolve-pr-details", resolve_pr_step["run"])
        self.assertIn('--pr-number "${{ inputs.pr_number }}"', resolve_pr_step["run"])
        self.assertIn('--github-output "${GITHUB_OUTPUT}"', resolve_pr_step["run"])
        self.assertEqual(snapshot_step["shell"], "bash")
        restore_step = steps["Restore workflow helper bundle"]
        self.assertEqual(restore_step["shell"], "bash")
        self.assertIn("restore-helper-bundle", restore_step["run"])
        self.assertIn('--helper-root ".workflow-action-update-agent-helper"', restore_step["run"])
        context_step = steps["Prepare stabilization context"]
        self.assertEqual(context_step["shell"], "bash")
        self.assertNotIn("uses", context_step)
        self.assertIn("python3 -m workflow_action_update_agent prepare-stabilization-context", context_step["run"])
        self.assertIn(
            'PYTHONPATH="${GITHUB_WORKSPACE}/.workflow-action-update-agent-helper/scripts/private',
            context_step["run"],
        )
        self.assertIn(
            '--profile-path ".workflow-action-update-agent-helper/${{ inputs.profile_path }}"',
            context_step["run"],
        )
        self.assertIn('--github-output "${GITHUB_OUTPUT}"', context_step["run"])
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
