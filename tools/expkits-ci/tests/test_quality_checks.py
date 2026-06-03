#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import importlib
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

FIXTURE_ROOT = Path(__file__).resolve().parent / "fixtures"


class DummyRepo:
    def __init__(self, *args, **kwargs):
        self.working_tree_dir = str(Path(__file__).resolve().parents[3])


sys.modules["argcomplete"] = types.SimpleNamespace(
    autocomplete=lambda *_args, **_kwargs: None,
)
sys.modules["git"] = types.SimpleNamespace(Repo=DummyRepo, GitCommandError=Exception)

quality_checks_module = importlib.import_module("expkits_ci.quality_checks")
QualityChecks = quality_checks_module.QualityChecks


class TestQualityChecks(unittest.TestCase):
    def setUp(self):
        self.quality_checks = QualityChecks()
        self.quality_checks.file_utils.filter_by_path_ending = Mock(
            side_effect=lambda files, _: files)

    def assert_formatter_failure_does_not_record_autofix(self, method_name, check_args):
        with patch.object(self.quality_checks, "record_autofix") as record_autofix:
            with patch(
                "expkits_ci.quality_checks.subprocess.run",
                side_effect=[
                    Mock(returncode=1, stdout="", stderr=""),
                    Mock(returncode=1, stdout="formatter failed", stderr=""),
                ],
            ):
                with self.assertLogs("expkits_ci", level="ERROR") as logs:
                    result = getattr(self.quality_checks, method_name)(
                        ["test.file"], *check_args)

        self.assertFalse(result)
        record_autofix.assert_not_called()
        self.assertIn("failed to format test.file", "\n".join(logs.output))

    def test_record_manual_fix_uses_lowercase_git_add(self):
        with self.assertLogs("expkits_ci", level="ERROR") as logs:
            self.quality_checks.record_manual_fix(
                "test.file", "tool", "Fix it.")

        self.assertIn("git add test.file", "\n".join(logs.output))
        self.assertNotIn("Git add test.file", "\n".join(logs.output))

    def test_clang_format_failure_does_not_record_autofix(self):
        self.assert_formatter_failure_does_not_record_autofix(
            "check_clang_format", [True, False])

    def test_cmake_format_failure_does_not_record_autofix(self):
        self.assert_formatter_failure_does_not_record_autofix(
            "check_cmake_format", [True, False])

    def test_shell_format_failure_does_not_record_autofix(self):
        self.assert_formatter_failure_does_not_record_autofix(
            "check_shell_format", [True])

    def test_get_detect_secrets_command_prefers_path_binary(self):
        with patch("expkits_ci.quality_checks.shutil.which", return_value="/usr/bin/detect-secrets-hook"):
            self.assertEqual(
                self.quality_checks.get_detect_secrets_command(),
                ["/usr/bin/detect-secrets-hook"],
            )

    def test_get_detect_secrets_command_falls_back_to_active_python(self):
        with patch("expkits_ci.quality_checks.shutil.which", return_value=None):
            self.assertEqual(
                self.quality_checks.get_detect_secrets_command(),
                [sys.executable, "-m", "detect_secrets.pre_commit_hook"],
            )

    def test_check_secrets_batches_files_and_uses_resolved_command(self):
        files = [f"file-{index}.txt" for index in range(55)]

        with patch("expkits_ci.quality_checks.os.path.isfile", return_value=True):
            with patch.object(self.quality_checks, "get_detect_secrets_command", return_value=["detect-secrets-hook"]):
                with patch(
                    "expkits_ci.quality_checks.subprocess.run",
                    side_effect=[
                        Mock(returncode=0, stdout="", stderr=""),
                        Mock(returncode=0, stdout="", stderr=""),
                    ],
                ) as subprocess_run:
                    result = self.quality_checks.check_secrets(files=files)

        self.assertTrue(result)
        self.assertEqual(subprocess_run.call_count, 2)
        first_cmd = subprocess_run.call_args_list[0].args[0]
        second_cmd = subprocess_run.call_args_list[1].args[0]
        self.assertEqual(first_cmd[:2], ["detect-secrets-hook", "--baseline"])
        self.assertEqual(second_cmd[:2], ["detect-secrets-hook", "--baseline"])
        self.assertEqual(len(first_cmd) - 3, 50)
        self.assertEqual(len(second_cmd) - 3, 5)

    def test_apply_license_header_keeps_cmake_content_adjacent_to_header(self):
        input_content = (FIXTURE_ROOT / "bad.CMakeLists.txt.input").read_text(encoding="utf-8")

        with tempfile.TemporaryDirectory() as temp_dir:
            target_file = Path(temp_dir) / "CMakeLists.txt"
            target_file.write_text(input_content, encoding="utf-8")

            with patch.object(self.quality_checks, "get_license_header", return_value="# Synthetic header\n"):
                with patch.object(self.quality_checks, "record_autofix") as record_autofix:
                    result = self.quality_checks.apply_license_header(
                        str(target_file),
                        input_content,
                    )
            rendered = target_file.read_text(encoding="utf-8")

        self.assertTrue(result)
        self.assertTrue(rendered.startswith("# Synthetic header\ncmake_minimum_required"))
        self.assertNotIn("# Synthetic header\n\ncmake_minimum_required", rendered)
        record_autofix.assert_called_once_with(
            str(target_file),
            "license-header",
            "added a missing header to",
        )


if __name__ == "__main__":
    unittest.main()
