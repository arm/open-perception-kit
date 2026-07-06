################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import sys
from pathlib import Path
import unittest
import argparse
import os
import tempfile
import urllib.error
import urllib.parse
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tests'))
from agent_workflow_test_support import (  # noqa: E402
    AGENT_REVIEW_PUBLISH_SCRIPT,
    AGENT_REVIEW_STATE,
    HELPER_GITHUB_WORKFLOWS,
    HELPER_STABILIZATION,
    HELPER_STABILIZATION_SCRIPT,
    OPENAI_AGENT_GITHUB_ACTIONS,
    PROFILE_FILE,
    REPAIR_BRANCH,
    STABILIZE_GOAL_TEMPLATE,
)


class WorkflowActionUpdateAgentStabilizationTests(unittest.TestCase):
    def test_stabilization_prompt_is_loaded_from_checked_in_template(self):
        source = HELPER_STABILIZATION_SCRIPT.read_text(encoding="utf-8")

        self.assertTrue(STABILIZE_GOAL_TEMPLATE.is_file())
        self.assertIn('render_markdown_template(\n        "stabilize-goal.md.in"', source)
        self.assertNotIn("Goal: address the latest standard Agent Review findings", source)
        self.assertIn(
            "Goal: address the latest standard Agent Review findings",
            STABILIZE_GOAL_TEMPLATE.read_text(encoding="utf-8"),
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

    def test_stabilizer_dispatch_uses_manual_front_door_workflow(self):
        with mock.patch.object(HELPER_GITHUB_WORKFLOWS, "dispatch_workflow_run") as dispatch_workflow_run:
            with mock.patch.object(
                HELPER_GITHUB_WORKFLOWS,
                "wait_for_dispatched_workflow_run",
                return_value="28000000001",
            ) as wait_for_dispatched_workflow_run:
                with mock.patch.object(HELPER_GITHUB_WORKFLOWS, "wait_for_workflow_run_completion"):
                    run_id = HELPER_GITHUB_WORKFLOWS.dispatch_stabilizer_workflow(
                        repository="Arm-Debug/amp-dev-forge",
                        pr_number="175",
                        head_sha="deadbeef",
                        source_run_id="12345",
                        ticket_id="EXPKITS-1007",
                        profile_path=".github/agent-runtime/workflow-action-update-agent/profiles/profile.json",
                        context_root=".agent-runtime/workflow-action-update-agent",
                        dispatch_ref="feature/test",
                        dispatch_nonce="nonce-1",
                    )

        self.assertEqual(run_id, "28000000001")
        self.assertEqual(
            dispatch_workflow_run.call_args.kwargs["workflow_file"],
            "workflow-action-update-agent.yml",
        )
        self.assertNotEqual(
            dispatch_workflow_run.call_args.kwargs["workflow_file"],
            "agent-stabilize-pr.yml",
        )
        self.assertEqual(
            wait_for_dispatched_workflow_run.call_args.kwargs["workflow_file"],
            "workflow-action-update-agent.yml",
        )

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

    def test_prepare_stabilization_context_accepts_artifact_findings_without_comment_reconciliation(self):
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
                "summary": "Fix the workflow issue.",
                "findings": [
                    {
                        "title": "Artifact finding",
                        "path": ".github/workflows/example.yml",
                        "body": "Use the canonical review artifact.",
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
            self.assertTrue((context_root / "review-state.json").is_file())

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
                        ticket_id="EXPKITS-1007",
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
