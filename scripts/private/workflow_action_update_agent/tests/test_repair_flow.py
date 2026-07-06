################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import sys
from pathlib import Path
import unittest
import argparse
import os
import subprocess
import tempfile
import textwrap
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tests'))
from agent_workflow_test_support import (  # noqa: E402
    CONTEXT_TEMPLATE,
    HELPER_REPAIR,
    HELPER_RUNTIME,
    HELPER_RUNTIME_SCRIPT,
    OPENAI_AGENT_GITHUB_ACTIONS,
    PROFILE_FILE,
    REPAIR_BRANCH,
    SAMPLE_TASK_REF,
    build_zip_archive,
    load_python_module,
)


class WorkflowActionUpdateAgentRepairTests(unittest.TestCase):
    def test_helper_script_uses_github_workspace_as_repo_root(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            with mock.patch.dict(os.environ, {"GITHUB_WORKSPACE": temp_dir}, clear=False):
                workspace_helper = load_python_module(
                    HELPER_RUNTIME_SCRIPT,
                    "workflow_action_update_agent_workspace_root",
                )

        workspace_root = Path(temp_dir).resolve()
        default_profile_path = ".github/agent-runtime/workflow-action-update-agent/profiles/profile.json"

        self.assertEqual(workspace_helper.REPO_ROOT, workspace_root)
        self.assertEqual(
            workspace_helper.DEFAULT_PROFILE_PATH,
            workspace_root / default_profile_path,
        )
        self.assertEqual(
            workspace_helper.default_profile_path_argument(),
            default_profile_path,
        )
        self.assertEqual(
            workspace_helper.resolve_repo_path(default_profile_path),
            workspace_root / default_profile_path,
        )
        self.assertEqual(
            workspace_helper.profile_config_root(
                ".workflow-action-update-agent-helper/.github/agent-runtime/workflow-action-update-agent/profiles/profile.json",
            ),
            workspace_root / ".workflow-action-update-agent-helper",
        )

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

    def test_resolve_inputs_uses_profile_branch_template(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            output_file = Path(temp_dir) / "outputs.txt"
            args = argparse.Namespace(
                profile_path=str(PROFILE_FILE),
                source_run_id="12345",
                target_branch="",
                task_ref="",
                current_ref_name="main",
                github_output=str(output_file),
            )
            run_payload = {
                "html_url": "https://github.com/Arm-Debug/amp-dev-forge/actions/runs/12345",
                "name": "Perception Experience Kit CI Pipeline",
                "conclusion": "failure",
                "head_branch": f"feature/{SAMPLE_TASK_REF}/topic",
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
            self.assertEqual(outputs["target_branch"], f"feature/{SAMPLE_TASK_REF}/topic")
            self.assertEqual(outputs["task_ref"], SAMPLE_TASK_REF)
            self.assertEqual(
                outputs["repair_branch"],
                REPAIR_BRANCH,
            )
            self.assertEqual(outputs["agent_model"], "gpt-5.5")

    def test_resolve_inputs_skips_local_run_without_source_run_or_task_ref(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            output_file = Path(temp_dir) / "outputs.txt"
            args = argparse.Namespace(
                profile_path=str(PROFILE_FILE),
                source_run_id="",
                target_branch="",
                task_ref="",
                current_ref_name="main",
                github_output=str(output_file),
            )

            with mock.patch.dict(os.environ, {"GITHUB_REPOSITORY": "Arm-Debug/amp-dev-forge"}, clear=False):
                result = HELPER_REPAIR.command_resolve_inputs(args)

            self.assertEqual(result, 0)
            outputs = dict(
                line.split("=", 1)
                for line in output_file.read_text(encoding="utf-8").splitlines()
                if line.strip()
            )
            self.assertEqual(outputs["should_run"], "false")
            self.assertEqual(outputs["skip_reason"], "No source run ID was provided.")
            self.assertEqual(outputs["task_ref"], "")
            self.assertEqual(outputs["repair_branch"], "")

    def test_resolve_inputs_fails_when_repair_run_has_no_task_ref_source(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            output_file = Path(temp_dir) / "outputs.txt"
            args = argparse.Namespace(
                profile_path=str(PROFILE_FILE),
                source_run_id="12345",
                target_branch="",
                task_ref="",
                current_ref_name="main",
                github_output=str(output_file),
            )
            run_payload = {
                "html_url": "https://github.com/Arm-Debug/amp-dev-forge/actions/runs/12345",
                "name": "Perception Experience Kit CI Pipeline",
                "conclusion": "failure",
                "head_branch": "feature/no-task-ref",
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
                    with self.assertRaisesRegex(ValueError, "requires a task reference"):
                        HELPER_REPAIR.command_resolve_inputs(args)

    def test_resolve_inputs_requires_source_pr_authorization_label(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            output_file = Path(temp_dir) / "outputs.txt"
            args = argparse.Namespace(
                profile_path=str(PROFILE_FILE),
                source_run_id="12345",
                target_branch="",
                task_ref="TASK-1",
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
                task_ref="TASK-1",
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
                task_ref="TASK-1",
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
            for command in HELPER_RUNTIME.profile_validation_commands(profile):
                self.assertIn(f"- `{command}`", validation)
            self.assertIn("- `artifacts/summary.txt`", inventory)

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
            task_ref="TASK-1",
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
                task_ref="TASK-1",
                body_file=str(body_file),
                pr_title_file=str(title_file),
                commit_subject_file=str(subject_file),
                commit_notes_file=str(notes_file),
                github_output=str(output_file),
            )

            commands = []

            def fake_run_command(command, *, capture_output=False, check=True, env=None):  # noqa: ANN001
                del capture_output, check, env
                commands.append(command)
                if command == [
                    "git",
                    "ls-remote",
                    "--heads",
                    "origin",
                    f"refs/heads/{REPAIR_BRANCH}",
                ]:
                    stdout = "cafebabe\trefs/heads/" + REPAIR_BRANCH + "\n"
                elif command == ["git", "rev-parse", "HEAD"]:
                    stdout = "feedface\n"
                else:
                    stdout = ""
                return subprocess.CompletedProcess(command, 0, stdout=stdout, stderr="")

            with mock.patch.object(HELPER_REPAIR, "run_command", side_effect=fake_run_command):
                with mock.patch.object(HELPER_REPAIR, "write_repair_metadata_files") as write_metadata:
                    result = HELPER_REPAIR.command_apply_repair_changes_and_push(args)

            self.assertEqual(result, 0)
            self.assertEqual(write_metadata.call_args.kwargs["target_branch"], "main")
            self.assertIn(["git", "checkout", "-B", REPAIR_BRANCH], commands)
            self.assertIn(
                [
                    "git",
                    "push",
                    f"--force-with-lease=refs/heads/{REPAIR_BRANCH}:cafebabe",
                    "--set-upstream",
                    "origin",
                    f"HEAD:refs/heads/{REPAIR_BRANCH}",
                ],
                commands,
            )
            outputs = dict(
                line.split("=", 1)
                for line in output_file.read_text(encoding="utf-8").splitlines()
                if line.strip()
            )
            self.assertEqual(outputs["head_sha"], "feedface")

    def test_create_draft_pr_updates_existing_repair_pr(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = Path(temp_dir)
            output_file = temp_root / "outputs.txt"
            body_file = temp_root / "body.md"
            title_file = temp_root / "title.txt"
            body_file.write_text("body\n", encoding="utf-8")
            title_file.write_text("Repair title\n", encoding="utf-8")
            args = argparse.Namespace(
                profile_path=str(PROFILE_FILE),
                body_file=str(body_file),
                pr_title_file=str(title_file),
                target_branch="main",
                repair_branch=REPAIR_BRANCH,
                github_output=str(output_file),
            )
            commands = []

            def fake_run_command(command, *, capture_output=False, check=True, env=None):  # noqa: ANN001
                del capture_output, check, env
                commands.append(command)
                stdout = "42\n" if command[:3] == ["gh", "pr", "view"] else ""
                return subprocess.CompletedProcess(command, 0, stdout=stdout, stderr="")

            with mock.patch.object(HELPER_REPAIR, "run_command", side_effect=fake_run_command):
                result = HELPER_REPAIR.command_create_draft_pr(args)

            self.assertEqual(result, 0)
            self.assertFalse(any(command[:3] == ["gh", "pr", "create"] for command in commands))
            self.assertIn(
                [
                    "gh",
                    "pr",
                    "edit",
                    "42",
                    "--base",
                    "main",
                    "--title",
                    "Repair title",
                    "--body-file",
                    str(body_file),
                ],
                commands,
            )
            self.assertIn(["gh", "pr", "edit", "42", "--add-label", "run-pek-ci"], commands)
            outputs = dict(
                line.split("=", 1)
                for line in output_file.read_text(encoding="utf-8").splitlines()
                if line.strip()
            )
            self.assertEqual(outputs["pr_number"], "42")

    def test_create_draft_pr_creates_when_repair_pr_is_missing(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            temp_root = Path(temp_dir)
            output_file = temp_root / "outputs.txt"
            body_file = temp_root / "body.md"
            title_file = temp_root / "title.txt"
            body_file.write_text("body\n", encoding="utf-8")
            title_file.write_text("Repair title\n", encoding="utf-8")
            args = argparse.Namespace(
                profile_path=str(PROFILE_FILE),
                body_file=str(body_file),
                pr_title_file=str(title_file),
                target_branch="main",
                repair_branch=REPAIR_BRANCH,
                github_output=str(output_file),
            )
            commands = []
            pr_view_calls = 0

            def fake_run_command(command, *, capture_output=False, check=True, env=None):  # noqa: ANN001
                nonlocal pr_view_calls
                del capture_output, check, env
                commands.append(command)
                if command[:3] == ["gh", "pr", "view"]:
                    pr_view_calls += 1
                    if pr_view_calls == 1:
                        return subprocess.CompletedProcess(command, 1, stdout="", stderr="not found\n")
                    return subprocess.CompletedProcess(command, 0, stdout="43\n", stderr="")
                return subprocess.CompletedProcess(command, 0, stdout="", stderr="")

            with mock.patch.object(HELPER_REPAIR, "run_command", side_effect=fake_run_command):
                result = HELPER_REPAIR.command_create_draft_pr(args)

            self.assertEqual(result, 0)
            self.assertIn(
                [
                    "gh",
                    "pr",
                    "create",
                    "--draft",
                    "--base",
                    "main",
                    "--head",
                    REPAIR_BRANCH,
                    "--title",
                    "Repair title",
                    "--body-file",
                    str(body_file),
                ],
                commands,
            )
            self.assertIn(["gh", "pr", "edit", "43", "--add-label", "run-pek-ci"], commands)
            outputs = dict(
                line.split("=", 1)
                for line in output_file.read_text(encoding="utf-8").splitlines()
                if line.strip()
            )
            self.assertEqual(outputs["pr_number"], "43")


if __name__ == "__main__":
    unittest.main()
