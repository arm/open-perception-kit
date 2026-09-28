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

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class OpkConfigCheckCliTest(unittest.TestCase):
    executable: Path
    repository_root: Path

    @classmethod
    def setUpClass(cls) -> None:
        cls.executable = Path(sys.argv[1]).resolve()
        cls.repository_root = Path(sys.argv[2]).resolve()

    def run_cli(self, *args: str) -> subprocess.CompletedProcess[str]:
        environment = dict(os.environ) | {
            "OPK_LOG_LEVEL": "3",
            "OPK_LOG_TARGETS": "stderr",
        }
        return subprocess.run(
            [str(self.executable), *args],
            check=False,
            capture_output=True,
            env=environment,
            text=True,
        )

    def test_help_is_written_to_stdout(self) -> None:
        result = self.run_cli("--help")

        self.assertEqual(result.returncode, 0)
        self.assertIn("Usage:", result.stdout)
        self.assertEqual(result.stderr, "")

    def test_invocation_error_is_written_to_stderr(self) -> None:
        result = self.run_cli()

        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, "")
        self.assertIn("E: opk-config-check:", result.stderr)
        self.assertIn("--root is required", result.stderr)
        self.assertIn("Usage:", result.stderr)
        self.assertTrue(result.stderr.endswith("\n"))

    def test_validation_failure_is_written_to_stderr(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            shutil.copytree(
                self.repository_root / "config/schemas",
                root / "config/schemas",
            )
            for name in ("invalid", "bad\nmodel"):
                model_dir = root / "config/models" / name
                model_dir.mkdir(parents=True)
                (model_dir / "model.json").write_text("{}")
            (root / "config/opchains").mkdir(parents=True)
            (root / "config/pipelines").mkdir(parents=True)

            result = self.run_cli("--root", str(root))

            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertIn(
                "E: config/models/invalid/model.json",
                result.stderr,
            )
            self.assertEqual(len(result.stderr.splitlines()), 2)
            self.assertIn(r"config/models/bad\nmodel/model.json", result.stderr)
            self.assertTrue(result.stderr.endswith("\n"))

    def test_version_compatibility_for_each_contract(self) -> None:
        sources = {
            "models/model.json": "models/yolo26n-320/model.json",
            "opchains/opchain.json": "models/yolo26n-320/opchain.json",
            "pipelines/demo.json": "pipelines/yolo26n-320.json",
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            shutil.copytree(self.repository_root / "config/schemas", root / "config/schemas")
            for name in ("models", "opchains", "pipelines"):
                (root / "config" / name).mkdir()
            for target, source in sources.items():
                path = root / "config" / target
                document = json.loads((self.repository_root / "config" / source).read_text())
                for version in ("1.0.0", "1.0.37", "1.4.2", "0.9.0", "2.0.0"):
                    with self.subTest(target=target, version=version):
                        document["version"] = version
                        path.write_text(json.dumps(document))
                        result = self.run_cli("--root", str(root))
                        if not version.startswith("1."):
                            self.assertEqual(result.returncode, 1, result.stdout)
                            self.assertIn("incompatible descriptor major", result.stderr)
                        else:
                            self.assertEqual(result.returncode, 0, result.stderr)
                            if version == "1.4.2":
                                self.assertIn("W:", result.stderr)
                                self.assertIn("minor version differs", result.stderr)
                                self.assertIn(target + ":/version", result.stderr)
                            else:
                                self.assertEqual(result.stderr, "")
                path.unlink()


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
