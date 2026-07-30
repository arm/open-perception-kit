#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import importlib.util
import subprocess
from pathlib import Path
import tempfile
import unittest
from unittest import mock


MODULE_PATH = Path(__file__).resolve().parents[1] / "github_pr_context.py"


def load_module():
    spec = importlib.util.spec_from_file_location("github_pr_context_under_test", MODULE_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load {MODULE_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


github_pr_context = load_module()


class GithubPrContextTests(unittest.TestCase):
    def test_resolve_pr_context_uses_one_json_query(self):
        with mock.patch.object(
            github_pr_context.subprocess,
            "run",
            return_value=subprocess.CompletedProcess(
                ["gh"],
                0,
                stdout='{"baseRefName":"main","headRefName":"feature/test","headRefOid":"deadbeef"}',
                stderr="",
            ),
        ) as run:
            context = github_pr_context.resolve_pr_context(
                pr_number="101",
                repo="Arm-Debug/amp-dev-forge",
            )

        self.assertEqual(
            run.call_args.args[0],
            [
                "gh",
                "pr",
                "view",
                "101",
                "--repo",
                "Arm-Debug/amp-dev-forge",
                "--json",
                "baseRefName,headRefName,headRefOid",
            ],
        )
        self.assertEqual(
            context,
            {
                "pr_number": "101",
                "base_ref": "main",
                "head_ref": "feature/test",
                "head_sha": "deadbeef",
            },
        )

    def test_resolve_pr_context_applies_explicit_manual_overrides(self):
        with mock.patch.object(
            github_pr_context.subprocess,
            "run",
            return_value=subprocess.CompletedProcess(
                ["gh"],
                0,
                stdout='{"baseRefName":"main","headRefName":"feature/test","headRefOid":"deadbeef"}',
                stderr="",
            ),
        ):
            context = github_pr_context.resolve_pr_context(
                pr_number="101",
                repo="Arm-Debug/amp-dev-forge",
                base_ref_override="release/next",
                head_ref_override="repair/pr-sample",
                head_sha_override="feedface",
            )

        self.assertEqual(
            context,
            {
                "pr_number": "101",
                "base_ref": "release/next",
                "head_ref": "repair/pr-sample",
                "head_sha": "feedface",
            },
        )

    def test_resolve_pr_context_rejects_sha_override_without_matching_head_ref(self):
        with mock.patch.object(
            github_pr_context.subprocess,
            "run",
            return_value=subprocess.CompletedProcess(
                ["gh"],
                0,
                stdout='{"baseRefName":"main","headRefName":"feature/test","headRefOid":"deadbeef"}',
                stderr="",
            ),
        ):
            with self.assertRaisesRegex(ValueError, "head-ref-override"):
                github_pr_context.resolve_pr_context(
                    pr_number="101",
                    repo="Arm-Debug/amp-dev-forge",
                    head_sha_override="feedface",
                )

    def test_write_outputs_uses_github_output_format(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            output_path = Path(temp_dir) / "outputs.txt"
            github_pr_context.write_outputs(
                {"base_ref": "main", "head_sha": "deadbeef"},
                str(output_path),
            )

            self.assertEqual(
                output_path.read_text(encoding="utf-8"),
                "base_ref=main\nhead_sha=deadbeef\n",
            )


if __name__ == "__main__":
    unittest.main()
