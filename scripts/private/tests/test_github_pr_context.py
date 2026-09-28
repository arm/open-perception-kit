#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

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
            "base_sha": "cafebabe",
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
                repo="arm/open-perception-kit",
            )

        self.assertEqual(
            read_pr_details.call_args,
            mock.call("101", repository="arm/open-perception-kit"),
        )
        self.assertEqual(
            context,
            {
                "pr_number": "101",
                "base_ref": "main",
                "base_sha": "cafebabe",
                "head_ref": "feature/test",
                "head_sha": "deadbeef",
            },
        )

    def test_resolve_pr_context_preserves_stacked_pr_parent(self):
        with mock.patch.object(
            github_pr_context,
            "read_pr_details",
            return_value=self.pr_details(target_branch="feature/parent"),
        ):
            context = github_pr_context.resolve_pr_context(
                pr_number="102",
                repo="arm/open-perception-kit",
            )

        self.assertEqual(context["base_ref"], "feature/parent")

    def test_resolve_pr_context_applies_explicit_manual_overrides(self):
        with (
            mock.patch.object(
                github_pr_context,
                "read_pr_details",
                return_value=self.pr_details(),
            ),
            mock.patch.object(
                github_pr_context,
                "github_api_json",
                return_value={"sha": "decafbad"},
            ) as github_api_json,
        ):
            context = github_pr_context.resolve_pr_context(
                pr_number="101",
                repo="arm/open-perception-kit",
                base_ref_override="release/next",
                head_ref_override="repair/pr-sample",
                head_sha_override="feedface",
            )

        self.assertEqual(
            context,
            {
                "pr_number": "101",
                "base_ref": "release/next",
                "base_sha": "decafbad",
                "head_ref": "repair/pr-sample",
                "head_sha": "feedface",
            },
        )
        github_api_json.assert_called_once_with(
            "repos/arm/open-perception-kit/commits/release%2Fnext"
        )

    def test_resolve_pr_context_prefers_a_head_ref_override_without_a_sha(self):
        with mock.patch.object(
            github_pr_context,
            "read_pr_details",
            return_value=self.pr_details(),
        ):
            context = github_pr_context.resolve_pr_context(
                pr_number="101",
                repo="arm/open-perception-kit",
                head_ref_override="repair/pr-sample",
            )

        self.assertEqual(context["head_ref"], "repair/pr-sample")
        self.assertEqual(context["head_sha"], "")

    def test_resolve_pr_context_rejects_sha_override_without_matching_head_ref(self):
        with mock.patch.object(
            github_pr_context,
            "read_pr_details",
            return_value=self.pr_details(),
        ):
            with self.assertRaisesRegex(ValueError, "head-ref-override"):
                github_pr_context.resolve_pr_context(
                    pr_number="101",
                    repo="arm/open-perception-kit",
                    head_sha_override="feedface",
                )

    def test_resolve_pr_context_rejects_missing_or_empty_refs(self):
        for field in ("target_branch", "base_sha", "head_branch", "head_sha"):
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
                            repo="arm/open-perception-kit",
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
