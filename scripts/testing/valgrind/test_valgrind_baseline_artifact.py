#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import contextlib
import importlib.util
import io
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT_PATH = Path(__file__).with_name("valgrind-baseline-artifact.py")
VALGRIND_WORKFLOW = Path(__file__).resolve().parents[3] / ".github/workflows/valgrind.yml"


def load_helper():
    spec = importlib.util.spec_from_file_location("valgrind_baseline_artifact", SCRIPT_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class TestValgrindBaselineArtifact(unittest.TestCase):
    def run_helper(
        self,
        command,
        runs=(),
        artifact_runs=(),
        success_runs_after_attempts=None,
        output_path=None,
        extra_env=None,
    ):
        env = {
            "GITHUB_REPOSITORY": "example/repo",
            "GITHUB_OUTPUT": str(output_path) if output_path else "",
            **(extra_env or {}),
        }
        with mock.patch.dict(os.environ, env, clear=False):
            helper = load_helper()

            attempts = 0

            def gh(*args):
                return "current-develop-sha"

            def gh_json(*args):
                nonlocal attempts
                if args[:2] == ("run", "list"):
                    if "--status" in args and success_runs_after_attempts is not None:
                        attempts += 1
                        return [] if attempts < 3 else success_runs_after_attempts
                    return runs
                run_id = int(args[1].split("/")[-2])
                artifacts = [{"name": "valgrind-baseline", "expired": False}] if run_id in artifact_runs else []
                return {"artifacts": artifacts}

            targets = {
                "locate": helper.locate_baseline,
                "publish": helper.publish_missing_baseline,
                "wait": helper.wait_for_baseline,
            }
            stdout = io.StringIO()
            stderr = io.StringIO()
            with mock.patch.object(helper, "gh", side_effect=gh), \
                    mock.patch.object(helper, "gh_json", side_effect=gh_json), \
                    mock.patch.object(helper.subprocess, "run") as subprocess_run, \
                    mock.patch.object(helper.time, "sleep"), \
                    contextlib.redirect_stdout(stdout), \
                    contextlib.redirect_stderr(stderr):
                code = targets[command]()

        return code, stdout.getvalue(), stderr.getvalue(), subprocess_run

    def test_locate_selects_first_current_head_run_with_available_artifact(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            output_path = Path(tmpdir) / "github-output"
            code, stdout, _, _ = self.run_helper(
                "locate",
                output_path=output_path,
                runs=[
                    {"databaseId": 303, "event": "workflow_dispatch", "status": "completed"},
                    {"databaseId": 202, "event": "push", "status": "completed"},
                ],
                artifact_runs={202},
            )

            self.assertEqual(code, 0)
            self.assertIn(
                "Using valgrind-baseline artifact from run 202 at develop current-develop-sha.",
                stdout,
            )
            self.assertEqual(output_path.read_text(encoding="utf-8"), "run-id=202\n")

    def test_locate_fails_when_current_head_has_no_available_artifact(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            output_path = Path(tmpdir) / "github-output"
            code, _, stderr, _ = self.run_helper(
                "locate",
                output_path=output_path,
                runs=[{"databaseId": 303, "event": "workflow_dispatch", "status": "completed"}],
            )

            self.assertEqual(code, 1)
            self.assertIn(
                "No available valgrind-baseline artifact found on develop at current-develop-sha.",
                stderr,
            )
            self.assertFalse(output_path.exists())

    def test_publish_skips_when_current_head_artifact_exists(self):
        code, stdout, _, subprocess_run = self.run_helper(
            "publish",
            runs=[{"databaseId": 202, "event": "workflow_dispatch", "status": "completed"}],
            artifact_runs={202},
        )

        self.assertEqual(code, 0)
        self.assertIn("Baseline artifact already exists in run 202.", stdout)
        subprocess_run.assert_not_called()

    def test_publish_ignores_pull_request_target_artifact_candidates(self):
        code, stdout, _, _ = self.run_helper(
            "publish",
            runs=[
                {"databaseId": 303, "event": "pull_request_target", "status": "completed"},
                {"databaseId": 202, "event": "workflow_dispatch", "status": "completed"},
            ],
            artifact_runs={202},
        )

        self.assertEqual(code, 0)
        self.assertIn("Baseline artifact already exists in run 202.", stdout)

    def test_publish_skips_when_current_head_run_is_active(self):
        code, stdout, _, subprocess_run = self.run_helper(
            "publish",
            runs=[{"databaseId": 404, "event": "workflow_dispatch", "status": "in_progress"}],
        )

        self.assertEqual(code, 0)
        self.assertIn("Baseline run 404 is already active.", stdout)
        subprocess_run.assert_not_called()

    def test_publish_dispatches_when_no_artifact_or_active_run_exists(self):
        code, _, _, subprocess_run = self.run_helper("publish", runs=[])

        self.assertEqual(code, 0)
        subprocess_run.assert_called_once_with(
            ["gh", "workflow", "run", "valgrind.yml", "--ref", "develop"],
            check=True,
        )

    def test_wait_polls_quietly_until_artifact_exists(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            output_path = Path(tmpdir) / "github-output"
            code, stdout, stderr, _ = self.run_helper(
                "wait",
                output_path=output_path,
                extra_env={
                    "VALGRIND_BASELINE_POLL_SECONDS": "0",
                    "VALGRIND_BASELINE_TIMEOUT_SECONDS": "10",
                },
                success_runs_after_attempts=[
                    {"databaseId": 202, "event": "workflow_dispatch", "status": "completed"}
                ],
                artifact_runs={202},
            )

            self.assertEqual(code, 0)
            self.assertIn("Waiting for valgrind-baseline artifact on develop current-develop-sha.", stdout)
            self.assertIn(
                "Using valgrind-baseline artifact from run 202 at develop current-develop-sha.",
                stdout,
            )
            self.assertNotIn("No available valgrind-baseline artifact", stderr)
            self.assertEqual(output_path.read_text(encoding="utf-8"), "run-id=202\n")

    def test_workflow_uses_python_baseline_helper(self):
        workflow = VALGRIND_WORKFLOW.read_text(encoding="utf-8")

        self.assertIn("pull_request_target:", workflow)
        self.assertIn("pull_request_target:\n    branches: [main, develop]", workflow)
        self.assertIn("actions: write", workflow)
        self.assertIn("github.event.pull_request.base.sha || github.sha", workflow)
        self.assertIn("valgrind-baseline-artifact.py publish", workflow)
        self.assertIn("valgrind-baseline-artifact.py locate", workflow)
        self.assertIn("valgrind-baseline-artifact.py wait", workflow)
        self.assertIn("auto-publish is only enabled for main/develop targets", workflow)
        self.assertIn("steps.valgrind_baseline.outputs.run-id || steps.waited_valgrind_baseline.outputs.run-id", workflow)


if __name__ == "__main__":
    unittest.main()
