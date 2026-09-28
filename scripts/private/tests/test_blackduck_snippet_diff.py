#!/usr/bin/env python3
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

"""Keep source additions scannable when a Git submodule becomes a directory."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import yaml


ROOT = Path(__file__).resolve().parents[3]


class SnippetDiffTests(unittest.TestCase):
    def test_workflow_omits_gitlink_metadata_but_keeps_all_source_changes(self):
        workflow = yaml.safe_load((ROOT / ".github/workflows/blackduck-scan.yml").read_text())
        step = next(step for step in workflow["jobs"]["snippets"]["steps"]
                    if step.get("name") == "Scan PR snippet delta and enforce policy")
        config = {key: str(value) for key, value in step["env"].items() if key.startswith("GIT_CONFIG_")}
        self.assertEqual(config, {
            "GIT_CONFIG_COUNT": "1", "GIT_CONFIG_KEY_0": "diff.ignoreSubmodules",
            "GIT_CONFIG_VALUE_0": "all",
        })
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)

            def git(*args, env=None):
                return subprocess.check_output(
                    ["git", "-c", "user.name=Snippet Test", "-c", "user.email=test@example.invalid",
                     "-c", "commit.gpgsign=false", *args], cwd=root, env=env,
                    stderr=subprocess.STDOUT,
                ).decode().strip()

            git("init", "-q")
            (root / "existing.py").write_text("before = 1\n")
            git("add", ".")
            git("commit", "-qm", "Initial source")
            git("update-index", "--add", "--cacheinfo", f"160000,{git('rev-parse', 'HEAD')},generator")
            git("commit", "-qm", "Pin generator")
            base = git("rev-parse", "HEAD")
            git("rm", "--cached", "generator")
            (root / "generator/engine").mkdir(parents=True)
            (root / "generator/gen.py").write_text("print('entrypoint')\n")
            (root / "generator/engine/parser.py").write_text("def parse(): return 1\n")
            (root / "existing.py").write_text("after = 2\n")
            git("add", ".")
            git("commit", "-qm", "Track generator sources")
            original = set(git("diff", "--name-only", f"{base}..HEAD").splitlines())
            self.assertIn("generator", original)
            scanned = set(git("diff", "--name-only", f"{base}..HEAD",
                              env={**os.environ, **config}).splitlines())
            self.assertEqual(scanned, {"existing.py", "generator/gen.py", "generator/engine/parser.py"})
            self.assertEqual(scanned, original - {"generator"})


if __name__ == "__main__":
    unittest.main()
