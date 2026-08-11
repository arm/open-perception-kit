#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class PekConfigCheckCliTest(unittest.TestCase):
    executable: Path
    repository_root: Path

    @classmethod
    def setUpClass(cls) -> None:
        cls.executable = Path(sys.argv[1]).resolve()
        cls.repository_root = Path(sys.argv[2]).resolve()

    def run_cli(self, *args: str) -> subprocess.CompletedProcess[str]:
        environment = dict(os.environ) | {
            "OPK_LOG_LEVEL": "1",
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
        self.assertIn("E: pek-config-check:", result.stderr)
        self.assertIn("--root is required", result.stderr)
        self.assertIn("Usage:", result.stderr)

    def test_validation_failure_is_written_to_stderr(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            shutil.copytree(
                self.repository_root / "config/schemas",
                root / "config/schemas",
            )
            model_dir = root / "config/models/invalid"
            model_dir.mkdir(parents=True)
            (root / "config/opchains").mkdir(parents=True)
            (model_dir / "model.json").write_text("{}")

            result = self.run_cli("--root", str(root))

            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertIn(
                "E: config/models/invalid/model.json",
                result.stderr,
            )


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
