# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

"""Check that production docs cannot select unmerged source commits."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import yaml


ROOT = Path(__file__).resolve().parents[3]


class DocsPublishWorkflowTests(unittest.TestCase):
    def test_main_history_guard_runs_before_documentation_scripts(self):
        workflow = yaml.safe_load(
            (ROOT / ".github/workflows/docs-publish-production.yml").read_text()
        )
        steps = workflow["jobs"]["publish-docs"]["steps"]
        checkout = next(step for step in steps if step["name"] == "Checkout code")
        guard = next(step for step in steps if step["name"] == "Require source from main")
        self.assertEqual(checkout["with"]["fetch-depth"], 0)
        self.assertEqual(steps.index(guard), steps.index(checkout) + 1)
        self.assertLess(
            steps.index(guard),
            next(index for index, step in enumerate(steps) if step["name"] == "Inject published build info"),
        )

        with tempfile.TemporaryDirectory() as directory:
            def git(*arguments):
                return subprocess.check_output(
                    ["git", "-c", "user.name=Workflow Test", "-c", "user.email=test@example.invalid",
                     "-c", "commit.gpgsign=false", *arguments],
                    cwd=directory, stderr=subprocess.STDOUT, text=True,
                ).strip()

            git("init", "-q", "--initial-branch=main")
            git("commit", "--allow-empty", "-qm", "Base")
            ancestor = git("rev-parse", "HEAD")
            git("checkout", "-qb", "feature")
            git("commit", "--allow-empty", "-qm", "Unmerged source")
            unmerged = git("rev-parse", "HEAD")
            git("checkout", "-q", "main")
            git("commit", "--allow-empty", "-qm", "Main update")
            main = git("rev-parse", "HEAD")
            git("update-ref", "refs/remotes/origin/main", main)

            for source, event, workflow_ref, expected in (
                (main, "workflow_dispatch", "refs/heads/main", 0),
                (ancestor, "workflow_dispatch", "refs/heads/main", 0),
                (unmerged, "workflow_dispatch", "refs/heads/main", 1),
                (main, "workflow_dispatch", "refs/heads/feature", 1),
                (main, "workflow_dispatch", "refs/tags/v1.0.0", 1),
                (main, "push", "refs/heads/main", 0),
                (unmerged, "push", "refs/heads/main", 1),
            ):
                with self.subTest(source=source, event=event, workflow_ref=workflow_ref):
                    git("checkout", "-q", "--detach", source)
                    result = subprocess.run(
                        ["bash", "-e", "-o", "pipefail", "-c", guard["run"]],
                        cwd=directory, capture_output=True, text=True, check=False,
                        env={**os.environ, "GITHUB_EVENT_NAME": event, "GITHUB_REF": workflow_ref},
                    )
                    self.assertEqual(result.returncode, expected, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
