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

"""Validate voice demo options without starting capture or loading model artifacts."""

from pathlib import Path
import subprocess
import sys
import unittest


EXECUTABLE = Path(sys.argv[1]).resolve()


class VoicePipelineCli(unittest.TestCase):
    def invoke(self, *arguments):
        return subprocess.run(
            [str(EXECUTABLE), *arguments],
            stdin=subprocess.DEVNULL,
            capture_output=True,
            text=True,
            check=False,
            timeout=10,
        )

    def test_help_is_available_without_a_terminal(self):
        result = self.invoke("--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--pipeline PATH", result.stdout)

    def test_pipeline_paths_reach_the_capture_boundary(self):
        for arguments in ([], ["--pipeline", "config/pipelines/custom-conversation.json"],
                          ["--pipeline", "/tmp/a pipeline with spaces.json"]):
            with self.subTest(arguments=arguments):
                result = self.invoke(*arguments)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn("requires an interactive terminal", result.stderr)

    def test_invalid_or_incomplete_options_fail_before_capture(self):
        for arguments in (
            ["--pipeline"],
            ["--pipeline", ""],
            ["--unknown", "pipeline.json"],
            ["--help", "--pipeline", "pipeline.json"],
            ["--pipeline", "pipeline.json", "unexpected"],
        ):
            with self.subTest(arguments=arguments):
                result = self.invoke(*arguments)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertIn("Usage:", result.stderr)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
