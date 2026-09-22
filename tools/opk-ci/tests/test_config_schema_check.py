#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from opk_ci.config_schema import (  # noqa: E402
    find_config_validator,
    run_config_validator,
)


class TestConfigSchemaCheck(unittest.TestCase):
    def test_validator_lookup_precedence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            staged = root / "tools/opk-config-check"
            staged.parent.mkdir(parents=True)
            staged.touch()
            validator = root / "development/build/meson-out/opk-config-check"
            validator.parent.mkdir(parents=True)
            validator.touch()

            with mock.patch("shutil.which", return_value="/usr/bin/opk-config-check"):
                self.assertEqual(find_config_validator(root), str(staged))
                staged.unlink()
                self.assertEqual(find_config_validator(root), str(validator))
                validator.unlink()
                self.assertEqual(find_config_validator(root), "/usr/bin/opk-config-check")

    def test_shared_validator_receives_repository_root(self):
        root = Path("/work")
        completed = subprocess.CompletedProcess([], 0, "Configuration descriptors are valid.\n")

        with mock.patch(
            "opk_ci.config_schema.find_config_validator",
            return_value="/work/tools/opk-config-check",
        ), mock.patch(
            "opk_ci.config_schema.subprocess.run",
            return_value=completed,
        ) as run:
            result = run_config_validator(root)

        self.assertIs(result, completed)
        run.assert_called_once_with(
            [
                "/work/tools/opk-config-check",
                "--root",
                "/work",
            ],
            cwd=root,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            encoding="utf-8",
            check=False,
        )

    def test_missing_shared_validator_is_reported(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch(
            "shutil.which", return_value=None
        ):
            with self.assertRaisesRegex(FileNotFoundError, "opk-config-check"):
                find_config_validator(directory)


if __name__ == "__main__":
    unittest.main()
