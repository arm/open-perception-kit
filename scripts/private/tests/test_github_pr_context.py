#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


MODULE_PATH = Path(__file__).resolve().parents[1] / "github_pr_context.py"
sys.path.insert(0, str(MODULE_PATH.parent))


def load_module():
    spec = importlib.util.spec_from_file_location("github_pr_context_under_test", MODULE_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load {MODULE_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


github_pr_context = load_module()


class GithubPrContextTests(unittest.TestCase):
    @staticmethod
    def pr_details(**overrides):
        details = {
            "target_branch": "main",
            "head_branch": "feature/test",
            "head_sha": "deadbeef",
        }
        details.update(overrides)
        return details

    def test_resolve_pr_context_uses_shared_github_api_query(self):
        with mock.patch.object(
            github_pr_context,
            "read_pr_details",
            return_value=self.pr_details(),
        ) as read_pr_details:
            context = github_pr_context.resolve_pr_context(
                pr_number="101",
                repo="Arm-Debug/amp-dev-forge",
            )

        self.assertEqual(
            read_pr_details.call_args,
            mock.call("101", repository="Arm-Debug/amp-dev-forge"),
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
            github_pr_context,
            "read_pr_details",
            return_value=self.pr_details(),
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
            github_pr_context,
            "read_pr_details",
            return_value=self.pr_details(),
        ):
            with self.assertRaisesRegex(ValueError, "head-ref-override"):
                github_pr_context.resolve_pr_context(
                    pr_number="101",
                    repo="Arm-Debug/amp-dev-forge",
                    head_sha_override="feedface",
                )

    def test_resolve_pr_context_rejects_missing_or_empty_refs(self):
        for field in ("target_branch", "head_branch", "head_sha"):
            for replacement in (None, ""):
                details = self.pr_details()
                if replacement is None:
                    del details[field]
                else:
                    details[field] = replacement
                with self.subTest(field=field, replacement=replacement), mock.patch.object(
                    github_pr_context,
                    "read_pr_details",
                    return_value=details,
                ):
                    with self.assertRaisesRegex(RuntimeError, "Incomplete pull request refs"):
                        github_pr_context.resolve_pr_context(
                            pr_number="101",
                            repo="Arm-Debug/amp-dev-forge",
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
