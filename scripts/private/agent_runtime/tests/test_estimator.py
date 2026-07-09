################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import importlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

estimator = importlib.import_module("agent_runtime.tasks.estimator")
set_run_context = importlib.import_module("agent_runtime.runtime_context").set_run_context
contracts = importlib.import_module("agent_runtime.contracts")
AgentCommand = contracts.AgentCommand
AgentInstance = contracts.AgentInstance
AgentTaskSettings = importlib.import_module("agent_runtime.config.task").AgentTaskSettings


class TaskEstimatorTests(unittest.TestCase):
    def test_prompt_limit_blocks_without_agent_estimation(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            set_run_context(Path(temp_dir), 10)
            settings = AgentTaskSettings(
                command=AgentCommand.REPAIR,
                agent_instance=AgentInstance.REPAIR,
                max_turns=30,
                max_prompt_chars=10,
            )

            manifest = estimator.build_task_manifest(
                AgentCommand.REPAIR,
                "x" * 11,
                settings,
                "gpt-test",
            )
            reasons = estimator.deterministic_task_limit_violations(manifest)

        self.assertIn("prompt has 11 characters", reasons[0])
        self.assertFalse(hasattr(estimator, "TaskEstimatorWorkflowTask"))
        self.assertFalse(hasattr(estimator, "TaskEstimate"))

    def test_review_diff_limits_are_advisory(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            subprocess.run(["git", "init"], cwd=repo_root, check=True, capture_output=True)
            subprocess.run(
                ["git", "config", "user.email", "agent@example.invalid"],
                cwd=repo_root,
                check=True,
            )
            subprocess.run(["git", "config", "user.name", "Agent"], cwd=repo_root, check=True)
            (repo_root / "README.md").write_text("base\n", encoding="utf-8")
            subprocess.run(["git", "add", "README.md"], cwd=repo_root, check=True)
            subprocess.run(["git", "commit", "-m", "base"], cwd=repo_root, check=True, capture_output=True)
            base_sha = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo_root, text=True).strip()

            for index in range(3):
                (repo_root / f"file-{index}.txt").write_text(f"{index}\n", encoding="utf-8")
            subprocess.run(["git", "add", "."], cwd=repo_root, check=True)
            subprocess.run(["git", "commit", "-m", "change"], cwd=repo_root, check=True, capture_output=True)
            head_sha = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo_root, text=True).strip()

            set_run_context(repo_root, 10)
            settings = AgentTaskSettings(
                command=AgentCommand.REVIEW,
                agent_instance=AgentInstance.REVIEW,
                max_turns=60,
                max_prompt_chars=None,
                max_review_files=2,
                max_review_changed_lines=2,
            )
            manifest = estimator.build_task_manifest(
                AgentCommand.REVIEW,
                "Review the pull request using the available review context.",
                settings,
                "gpt-test",
                base_sha=base_sha,
                head_sha=head_sha,
            )
            reasons = estimator.deterministic_task_limit_violations(manifest)
            advisory_reasons = estimator.task_estimate_advisory_reasons(manifest)

        self.assertEqual(reasons, [])
        self.assertIn("review scope touches 3 files", advisory_reasons[0])
        self.assertIn("review scope changes 3 lines", advisory_reasons[1])

    def test_turn_estimation_is_not_agent_backed(self):
        manifest = {
            "prompt_chars": 10,
            "limits": {
                "max_turns": 60,
                "max_prompt_chars": 100,
                "max_review_files": None,
                "max_review_changed_lines": None,
            },
            "git_metrics": {
                "total_diff_files": 0,
                "total_diff_changed_lines": 0,
            },
        }

        self.assertEqual(estimator.task_estimate_block_reasons(manifest), [])
        self.assertEqual(estimator.task_estimate_advisory_reasons(manifest), [])


if __name__ == "__main__":
    unittest.main()
