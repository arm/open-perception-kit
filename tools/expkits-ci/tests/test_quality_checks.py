#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import importlib
import json
import os
import re
import shutil
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


def import_quality_checks_module():
    try:
        return importlib.import_module("expkits_ci.quality_checks")
    except ModuleNotFoundError:
        stale_modules = {
            module_name: sys.modules.pop(module_name, None)
            for module_name in (
                "expkits_ci",
                "expkits_ci.expkits_ci",
                "expkits_ci.file_utils",
                "expkits_ci.license_template_manager",
                "expkits_ci.quality_checks",
            )
        }
        try:
            with patch.dict(
                sys.modules,
                {
                    "argcomplete": types.SimpleNamespace(
                        autocomplete=lambda *_args, **_kwargs: None,
                    ),
                    "git": types.SimpleNamespace(
                        Repo=DummyRepo,
                        GitCommandError=Exception,
                        BadName=ValueError,
                    ),
                },
            ):
                return importlib.import_module("expkits_ci.quality_checks")
        finally:
            for module_name in (
                "expkits_ci.quality_checks",
                "expkits_ci.license_template_manager",
                "expkits_ci.file_utils",
                "expkits_ci.expkits_ci",
                "expkits_ci",
            ):
                sys.modules.pop(module_name, None)
            for module_name, module in stale_modules.items():
                if module is not None:
                    sys.modules[module_name] = module


quality_checks_module = import_quality_checks_module()
QualityChecks = quality_checks_module.QualityChecks
FileUtils = quality_checks_module.FileUtils


class TestQualityChecks(unittest.TestCase):
    def setUp(self):
        self.quality_checks = QualityChecks()
        self.quality_checks.file_utils.filter_by_path_ending = Mock(
            side_effect=lambda files, _: files)

    def require_actionlint(self):
        missing = [
            tool for tool in ("actionlint", "shellcheck", "pyflakes")
            if shutil.which(tool) is None
        ]
        if missing:
            self.skipTest(f"actionlint toolchain is unavailable: {', '.join(missing)}")

    def write_actionlint_fixture(self, temp_dir, fixture_name, workflow_name):
        workflow = Path(temp_dir) / ".github" / "workflows" / workflow_name
        workflow.parent.mkdir(parents=True)
        workflow.write_text(
            (FIXTURE_ROOT / "actionlint" / fixture_name).read_text(encoding="utf-8"),
            encoding="utf-8",
        )
        self.quality_checks.file_utils.get_project_root = Mock(return_value=temp_dir)
        return f".github/workflows/{workflow_name}"

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

    def test_clang_format_failure_does_not_record_autofix(self):
        self.assert_formatter_failure_does_not_record_autofix(
            "check_clang_format", [True, False])

    def test_cmake_format_failure_does_not_record_autofix(self):
        self.assert_formatter_failure_does_not_record_autofix(
            "check_cmake_format", [True, False])

    def test_shell_format_failure_does_not_record_autofix(self):
        self.assert_formatter_failure_does_not_record_autofix(
            "check_shell_format", [True])

    def assert_formatter_check_logs_captured_output(self, method_name, check_args):
        with patch.object(self.quality_checks, "record_manual_fix") as record_manual_fix:
            with patch(
                "expkits_ci.quality_checks.subprocess.run",
                return_value=Mock(returncode=1, stdout="formatter diff\n", stderr=""),
            ):
                with self.assertLogs("expkits_ci", level="ERROR") as logs:
                    result = getattr(self.quality_checks, method_name)(
                        ["test.file"], *check_args)

        self.assertFalse(result)
        record_manual_fix.assert_called_once()
        self.assertIn("formatter diff", "\n".join(logs.output))

    def test_clang_format_check_logs_captured_output(self):
        self.assert_formatter_check_logs_captured_output(
            "check_clang_format", [False, False])

    def test_cmake_format_check_logs_captured_output(self):
        self.assert_formatter_check_logs_captured_output(
            "check_cmake_format", [False, False])

    def test_python_format_check_logs_captured_output(self):
        self.assert_formatter_check_logs_captured_output(
            "check_python_format", [False, False])

    def test_python_format_check_logs_stderr_when_autopep8_errors(self):
        with patch(
            "expkits_ci.quality_checks.subprocess.run",
            return_value=Mock(returncode=2, stdout="", stderr="autopep8 exploded\n"),
        ):
            with self.assertLogs("expkits_ci", level="ERROR") as logs:
                result = self.quality_checks.check_python_format(
                    ["test.file"], False, False)

        self.assertFalse(result)
        self.assertIn("autopep8 exploded", "\n".join(logs.output))

    def test_shell_format_check_logs_captured_output(self):
        self.assert_formatter_check_logs_captured_output(
            "check_shell_format", [False])

    def test_check_branch_naming_accepts_supported_branches(self):
        valid_branches = [
            "develop",
            "feature/EXPKITS-4242",
            "feature/EXPKITS-4242/ticket-description",  # pragma: allowlist secret
            "bugfix/EXPKITS-4242/fix-timeout",
            "hotfix/EXPKITS-4242/fix-release",
            "release/EXPKITS-4242-create-release-1.2.3",
            "dependabot/github_actions/actions-checkout-7",
        ]

        for branch_name in valid_branches:
            with self.subTest(branch_name=branch_name):
                fake_repo = Mock()
                fake_repo.active_branch.name = branch_name

                with patch.object(quality_checks_module, "Repo", return_value=fake_repo):
                    self.assertTrue(QualityChecks.check_branch_naming())

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

    def test_check_github_actions_runs_actionlint_on_workflow_files(self):
        self.quality_checks.file_utils.get_project_root = Mock(return_value="/work")

        with patch.object(
            quality_checks_module.shutil,
            "which",
            side_effect=lambda tool: f"/usr/bin/{tool}",
        ):
            with patch.object(quality_checks_module.os.path, "isfile", return_value=True):
                with patch.object(
                    quality_checks_module.subprocess,
                    "run",
                    return_value=Mock(returncode=0, stdout=""),
                ) as subprocess_run:
                    result = self.quality_checks.check_github_actions([
                        ".github/workflows/pek-ci.yml",
                        "./.github/workflows/workflow-audit.yml",
                        "/work/.github/workflows/docker-scout.yaml",
                        "README.md",
                    ])

        self.assertTrue(result)
        self.assertEqual(
            subprocess_run.call_args.args[0],
            [
                "/usr/bin/actionlint",
                "-shellcheck",
                "/usr/bin/shellcheck",
                "-pyflakes",
                "/usr/bin/pyflakes",
                "-config-file",
                ".github/actionlint.yaml",
                ".github/workflows/pek-ci.yml",
                ".github/workflows/workflow-audit.yml",
                ".github/workflows/docker-scout.yaml",
            ],
        )
        self.assertEqual(subprocess_run.call_args.kwargs["cwd"], "/work")

    def test_check_github_actions_skips_non_workflow_files(self):
        self.quality_checks.file_utils.get_project_root = Mock(return_value="/work")

        with patch.object(quality_checks_module.shutil, "which") as which:
            with patch.object(quality_checks_module.os.path, "isfile", return_value=True):
                with patch.object(
                    quality_checks_module.subprocess,
                    "run",
                    return_value=Mock(returncode=0, stdout=""),
                ) as subprocess_run:
                    result = self.quality_checks.check_github_actions([
                        "README.md",
                    ])

        self.assertTrue(result)
        which.assert_not_called()
        subprocess_run.assert_not_called()

    def test_check_github_actions_lints_all_workflows_when_config_changes(self):
        self.quality_checks.file_utils.get_project_root = Mock(return_value="/work")

        with patch.object(
            quality_checks_module.shutil,
            "which",
            side_effect=lambda tool: f"/usr/bin/{tool}",
        ):
            with patch.object(quality_checks_module.os.path, "isfile", return_value=True):
                with patch.object(
                    quality_checks_module.glob,
                    "glob",
                    side_effect=[
                        ["/work/.github/workflows/ci.yml"],
                        ["/work/.github/workflows/release.yaml"],
                    ],
                ):
                    with patch.object(
                        quality_checks_module.subprocess,
                        "run",
                        return_value=Mock(returncode=0, stdout=""),
                    ) as subprocess_run:
                        result = self.quality_checks.check_github_actions([
                            ".github/actionlint.yaml",
                        ])

        self.assertTrue(result)
        self.assertEqual(
            subprocess_run.call_args.args[0],
            [
                "/usr/bin/actionlint",
                "-shellcheck",
                "/usr/bin/shellcheck",
                "-pyflakes",
                "/usr/bin/pyflakes",
                "-config-file",
                ".github/actionlint.yaml",
                ".github/workflows/ci.yml",
                ".github/workflows/release.yaml",
            ],
        )

    def test_check_github_actions_fails_when_toolchain_is_incomplete(self):
        self.quality_checks.file_utils.get_project_root = Mock(return_value="/work")

        for missing in ("actionlint", "shellcheck", "pyflakes"):
            with self.subTest(missing=missing):
                with patch.object(
                    quality_checks_module.shutil,
                    "which",
                    side_effect=lambda tool: None if tool == missing else f"/usr/bin/{tool}",
                ):
                    with self.assertLogs("expkits_ci", level="ERROR") as logs:
                        result = self.quality_checks.check_github_actions([
                            ".github/workflows/ci.yml",
                        ])

                self.assertFalse(result)
                self.assertIn(f"{missing} is not available on PATH.", "\n".join(logs.output))

    def test_check_github_actions_fails_when_actionlint_finds_errors(self):
        self.quality_checks.file_utils.get_project_root = Mock(return_value="/work")

        with patch.object(
            quality_checks_module.shutil,
            "which",
            side_effect=lambda tool: f"/usr/bin/{tool}",
        ):
            with patch.object(quality_checks_module.os.path, "isfile", return_value=True):
                with patch.object(
                    quality_checks_module.subprocess,
                    "run",
                    return_value=Mock(returncode=1, stdout="workflow error\n"),
                ):
                    with self.assertLogs("expkits_ci", level="ERROR") as logs:
                        result = self.quality_checks.check_github_actions([
                            ".github/workflows/bad.yml",
                        ])

        self.assertFalse(result)
        self.assertIn("workflow error", "\n".join(logs.output))

    def test_check_github_actions_accepts_checked_in_good_fixture(self):
        self.require_actionlint()

        with tempfile.TemporaryDirectory() as temp_dir:
            workflow = self.write_actionlint_fixture(
                temp_dir,
                "good-workflow.yml",
                "good.yml",
            )
            result = self.quality_checks.check_github_actions([workflow])

        self.assertTrue(result)

    def test_check_github_actions_rejects_checked_in_bad_fixture(self):
        self.require_actionlint()

        with tempfile.TemporaryDirectory() as temp_dir:
            workflow = self.write_actionlint_fixture(
                temp_dir,
                "bad-workflow.yml",
                "bad.yml",
            )
            with self.assertLogs("expkits_ci", level="ERROR") as logs:
                result = self.quality_checks.check_github_actions([workflow])

        self.assertFalse(result)
        self.assertIn("syntax-check", "\n".join(logs.output))

    def test_check_github_actions_accepts_checked_in_valgrind_workflow(self):
        self.require_actionlint()
        self.quality_checks.file_utils.get_project_root = Mock(
            return_value=str(Path(__file__).resolve().parents[3])
        )

        result = self.quality_checks.check_github_actions([
            ".github/workflows/valgrind.yml",
        ])

        self.assertTrue(result)

    def test_check_config_schema_runs_shared_validator(self):
        run_config_validator = Mock(
            return_value=Mock(
                returncode=0,
                stdout="Configuration descriptors are valid.\n",
            )
        )
        config_schema = types.ModuleType("expkits_ci.config_schema")
        config_schema.run_config_validator = run_config_validator
        self.quality_checks.file_utils.get_project_root = Mock(return_value="/work")

        with patch.dict(sys.modules, {"expkits_ci.config_schema": config_schema}):
            result = self.quality_checks.check_config_schema()

        self.assertTrue(result)
        run_config_validator.assert_called_once_with("/work")

    def test_check_config_schema_reports_validator_failures(self):
        failures = (
            Mock(return_value=Mock(returncode=1, stdout="invalid descriptor\n")),
            Mock(side_effect=FileNotFoundError("pek-config-check is unavailable")),
        )
        self.quality_checks.file_utils.get_project_root = Mock(return_value="/work")

        for run_config_validator in failures:
            with self.subTest(side_effect=run_config_validator.side_effect):
                config_schema = types.ModuleType("expkits_ci.config_schema")
                config_schema.run_config_validator = run_config_validator
                with patch.dict(sys.modules, {"expkits_ci.config_schema": config_schema}):
                    with self.assertLogs("expkits_ci", level="ERROR"):
                        result = self.quality_checks.check_config_schema()

                self.assertFalse(result)
                run_config_validator.assert_called_once_with("/work")

    def test_file_filter_does_not_ignore_dotgithub_as_dotgit(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            for file_path in (
                ".github/workflows/ci.yml",
                ".git/config",
                "deps/data.txt",
                "development/build/out.txt",
                "development/building/out.txt",
            ):
                target = root / file_path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text("", encoding="utf-8")

            old_cwd = os.getcwd()
            try:
                os.chdir(temp_dir)
                result = FileUtils.filter_existing_files(
                    [
                        ".github/workflows/ci.yml",
                        ".git/config",
                        "deps/data.txt",
                        "development/build/out.txt",
                        "development/building/out.txt",
                    ],
                    ["./.git", "./deps", "./development/build"],
                )
            finally:
                os.chdir(old_cwd)

        self.assertEqual(
            result,
            [".github/workflows/ci.yml", "development/building/out.txt"],
        )

    def test_clang_tidy_limits_diagnostics_to_project_sources(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            project_root = Path(temp_dir)
            build_dir = project_root / "development" / "build"
            source_file = project_root / "development" / "common" / "pek" / "Result.cpp"
            build_dir.mkdir(parents=True)
            source_file.parent.mkdir(parents=True)
            source_file.write_text("", encoding="utf-8")
            (project_root / ".clang-tidy").write_text("---\nChecks: '-*'\n", encoding="utf-8")
            (build_dir / "compile_commands.json").write_text(
                json.dumps([{
                    "directory": str(build_dir),
                    "file": str(source_file),
                    "command": f"c++ -c {source_file}",
                }]),
                encoding="utf-8",
            )
            self.quality_checks.file_utils.get_project_root = Mock(
                return_value=str(project_root)
            )

            with patch.object(
                self.quality_checks,
                "_resolve_clang_tidy_binary",
                return_value="/usr/bin/clang-tidy",
            ), patch.object(
                quality_checks_module.subprocess,
                "run",
                return_value=Mock(returncode=0, stdout="", stderr=""),
            ) as subprocess_run:
                result = self.quality_checks.check_clang_tidy(
                    ["development/common/pek/Result.cpp"],
                    compile_commands_dir=str(build_dir),
                )

        self.assertTrue(result)
        command = subprocess_run.call_args.args[0]
        line_filter_arg = next(
            argument for argument in command
            if argument.startswith("--line-filter=")
        )
        line_filter = json.loads(line_filter_arg.split("=", 1)[1])
        project_file_pattern = re.compile(line_filter[0]["name"])
        self.assertRegex("../common/pek/Result.h", project_file_pattern)
        self.assertRegex("../config-validator/Validator.cpp", project_file_pattern)
        self.assertRegex("/work/development/runtime/Result.cpp", project_file_pattern)
        self.assertRegex("../ops-onnx/Inference.cpp", project_file_pattern)
        self.assertNotRegex("../subprojects/fmt/include/fmt/base.h", project_file_pattern)

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

    def test_agent_runtime_static_analysis_runs_shared_script_for_pr_target(self):
        with patch.object(quality_checks_module.FileUtils, "get_project_root", return_value="/work"):
            with patch(
                "expkits_ci.quality_checks.subprocess.run",
                return_value=Mock(returncode=0, stdout="ok\n", stderr=""),
            ) as subprocess_run:
                result = self.quality_checks.check_agent_runtime_static_analysis(
                    ["scripts/private/agent_runtime/openai_agent_runner.py"],
                    pr_target_branch="main",
                )

        self.assertTrue(result)
        self.assertEqual(
            subprocess_run.call_args.args[0],
            [
                sys.executable,
                "-m",
                "expkits_ci.agent_static_analysis",
                "--base-ref",
                "origin/main",
            ],
        )
        self.assertEqual(subprocess_run.call_args.kwargs["cwd"], "/work")
        self.assertIn("/work/tools/expkits-ci", subprocess_run.call_args.kwargs["env"]["PYTHONPATH"])

    def test_agent_runtime_static_analysis_checks_deleted_pr_paths(self):
        with patch.object(quality_checks_module.FileUtils, "get_project_root", return_value="/work"):
            with patch(
                "expkits_ci.quality_checks.subprocess.run",
                side_effect=[
                    Mock(returncode=0, stdout="D\0scripts/private/agent_runtime/task.py\0", stderr=""),
                    Mock(returncode=0, stdout="", stderr=""),
                ],
            ) as subprocess_run:
                result = self.quality_checks.check_agent_runtime_static_analysis(
                    ["docs/readme.md"],
                    pr_target_branch="main",
                )

        self.assertTrue(result)
        self.assertEqual(
            subprocess_run.call_args_list[0].args[0],
            ["git", "diff", "--name-status", "-z", "origin/main...HEAD"],
        )
        self.assertEqual(
            subprocess_run.call_args_list[1].args[0],
            [
                sys.executable,
                "-m",
                "expkits_ci.agent_static_analysis",
                "--base-ref",
                "origin/main",
            ],
        )
        self.assertEqual(subprocess_run.call_args_list[1].kwargs["cwd"], "/work")
        self.assertIn("/work/tools/expkits-ci", subprocess_run.call_args_list[1].kwargs["env"]["PYTHONPATH"])

    def test_apply_license_header_keeps_cmake_content_adjacent_to_header_when_cmake_config_is_missing(self):
        input_content = (FIXTURE_ROOT / "cmake" / "bad.CMakeLists.txt.input").read_text(encoding="utf-8")

        with tempfile.TemporaryDirectory() as temp_dir:
            target_file = Path(temp_dir) / "CMakeLists.txt"
            target_file.write_text(input_content, encoding="utf-8")

            with patch("expkits_ci.quality_checks.os.path.isfile", return_value=False):
                with patch("expkits_ci.quality_checks.subprocess.run") as subprocess_run:
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
        subprocess_run.assert_not_called()
        record_autofix.assert_called_once_with(
            str(target_file),
            "license-header",
            "added a missing header to",
        )

    def test_get_license_header_uses_literal_hash_borders_for_cmake_files(self):
        self.assertEqual(
            self.quality_checks.get_license_header("CMakeLists.txt"),
            "################################################################\n"
            "# Copyright (C) 2025 Arm Limited. All rights reserved.\n"
            "################################################################\n",
        )

    def test_apply_license_header_reformats_cmake_file_when_config_is_available(self):
        input_content = (FIXTURE_ROOT / "cmake" / "bad.CMakeLists.txt.input").read_text(encoding="utf-8")

        with tempfile.TemporaryDirectory() as temp_dir:
            target_file = Path(temp_dir) / "CMakeLists.txt"
            target_file.write_text(input_content, encoding="utf-8")

            def fake_cmake_format(cmd, stdout=None, stderr=None, encoding=None):
                self.assertEqual(
                    cmd,
                    ["cmake-format", "-c", ".cmake-format.yaml", "-i", str(target_file)],
                )
                target_file.write_text(
                    "# Synthetic header\ncmake_minimum_required(VERSION 3.20)\n",
                    encoding="utf-8",
                )
                return Mock(returncode=0, stdout="")

            with patch("expkits_ci.quality_checks.os.path.isfile", return_value=True):
                with patch.object(self.quality_checks, "get_license_header", return_value="# Synthetic header\n"):
                    with patch("expkits_ci.quality_checks.subprocess.run", side_effect=fake_cmake_format) as subprocess_run:
                        with patch.object(self.quality_checks, "record_autofix") as record_autofix:
                            result = self.quality_checks.apply_license_header(
                                str(target_file),
                                input_content,
                            )
            rendered = target_file.read_text(encoding="utf-8")

        self.assertTrue(result)
        self.assertEqual(rendered, "# Synthetic header\ncmake_minimum_required(VERSION 3.20)\n")
        self.assertEqual(subprocess_run.call_count, 1)
        record_autofix.assert_called_once_with(
            str(target_file),
            "license-header",
            "added a missing header to",
        )

    def test_apply_license_header_does_not_record_autofix_when_cmake_stabilization_fails(self):
        input_content = (FIXTURE_ROOT / "cmake" / "bad.CMakeLists.txt.input").read_text(encoding="utf-8")

        with tempfile.TemporaryDirectory() as temp_dir:
            target_file = Path(temp_dir) / "CMakeLists.txt"
            target_file.write_text(input_content, encoding="utf-8")

            with patch("expkits_ci.quality_checks.os.path.isfile", return_value=True):
                with patch(
                    "expkits_ci.quality_checks.subprocess.run",
                    return_value=Mock(returncode=1, stdout="formatter failed\n"),
                ) as subprocess_run:
                    with patch.object(self.quality_checks, "get_license_header", return_value="# Synthetic header\n"):
                        with patch.object(self.quality_checks, "record_autofix") as record_autofix:
                            with self.assertLogs("expkits_ci", level="ERROR") as logs:
                                result = self.quality_checks.apply_license_header(
                                    str(target_file),
                                    input_content,
                                )
            rendered = target_file.read_text(encoding="utf-8")

        self.assertFalse(result)
        self.assertTrue(rendered.startswith("# Synthetic header\ncmake_minimum_required"))
        subprocess_run.assert_called_once_with(
            ["cmake-format", "-c", ".cmake-format.yaml", "-i", str(target_file)],
            stdout=quality_checks_module.subprocess.PIPE,
            stderr=quality_checks_module.subprocess.STDOUT,
            encoding="utf-8",
        )
        self.assertIn("failed to stabilize", "\n".join(logs.output))
        record_autofix.assert_not_called()

    def make_repo_with_head_commit(self, message, hexsha="a" * 40):
        commit = types.SimpleNamespace(message=message, hexsha=hexsha)
        return types.SimpleNamespace(head=types.SimpleNamespace(commit=commit))

    def test_check_commit_messages_on_ci_allows_merge_commit_without_task_line(self):
        repo = self.make_repo_with_head_commit("Merge branch 'main' into feature/EXPKITS-973/pr-quality-gate\n")

        with patch("expkits_ci.quality_checks.Repo", return_value=repo):
            result = self.quality_checks.check_commit_messages_on_ci()

        self.assertTrue(result)

    def test_check_commit_messages_on_ci_allows_jira_subject_prefix_with_body(self):
        repo = self.make_repo_with_head_commit(
            "EXPKITS-1124: Bump actions/checkout from 6 to 7\n\n"
            "Bumps actions/checkout from 6 to 7.\n"
        )

        with patch("expkits_ci.quality_checks.Repo", return_value=repo):
            result = self.quality_checks.check_commit_messages_on_ci()

        self.assertTrue(result)

    def test_check_commit_messages_on_ci_rejects_non_git_generated_merge_subject_without_task_line(self):
        repo = self.make_repo_with_head_commit("Merge branch optimization\n")

        with patch("expkits_ci.quality_checks.Repo", return_value=repo):
            result = self.quality_checks.check_commit_messages_on_ci()

        self.assertFalse(result)

    def test_check_commit_messages_on_ci_allows_copilot_autofix_commit_without_task_line(self):
        repo = self.make_repo_with_head_commit(
            "Escaping URI string for TURN server\n\n"
            "Co-authored-by: Copilot Autofix powered by AI "
            "<175728472+Copilot@users.noreply.github.com>\n"
        )

        with patch("expkits_ci.quality_checks.Repo", return_value=repo):
            result = self.quality_checks.check_commit_messages_on_ci()

        self.assertTrue(result)

    def test_check_commit_messages_on_ci_rejects_regular_commit_without_task_line(self):
        repo = self.make_repo_with_head_commit("Regular fix without jira line\n")

        with patch("expkits_ci.quality_checks.Repo", return_value=repo):
            result = self.quality_checks.check_commit_messages_on_ci()

        self.assertFalse(result)

    def test_check_commit_messages_on_ci_excludes_commits_already_on_develop(self):
        head = types.SimpleNamespace(hexsha="c" * 40)
        main = types.SimpleNamespace(hexsha="a" * 40)
        develop = types.SimpleNamespace(hexsha="b" * 40)
        main_base = types.SimpleNamespace(hexsha="e" * 40)
        develop_base = types.SimpleNamespace(hexsha="f" * 40)
        commit = types.SimpleNamespace(
            message="Prepare release\n\nTask: EXPKITS-1191\n",
            hexsha="d" * 40,
        )
        repo = Mock()
        repo.head.commit = head
        repo.commit.side_effect = lambda ref: {
            "origin/main": main,
            "origin/develop": develop,
        }[ref]
        repo.merge_base.side_effect = [[main_base], [develop_base]]
        repo.is_ancestor.return_value = True
        repo.iter_commits.return_value = [commit]

        with patch("expkits_ci.quality_checks.Repo", return_value=repo):
            result = self.quality_checks.check_commit_messages_on_ci(target_branch="main")

        self.assertTrue(result)
        repo.git.fetch.assert_called_once_with(
            "origin",
            "+refs/heads/develop:refs/remotes/origin/develop",
        )
        repo.is_ancestor.assert_called_once_with(main_base, develop_base)
        repo.iter_commits.assert_called_once_with(f"{develop_base.hexsha}..HEAD")

    def test_check_commit_messages_on_ci_keeps_main_base_for_hotfix(self):
        head = types.SimpleNamespace(hexsha="c" * 40)
        main = types.SimpleNamespace(hexsha="a" * 40)
        develop = types.SimpleNamespace(hexsha="b" * 40)
        main_base = types.SimpleNamespace(hexsha="f" * 40)
        develop_base = types.SimpleNamespace(hexsha="e" * 40)
        commit = types.SimpleNamespace(
            message="Prepare hotfix\n\nTask: EXPKITS-1191\n",
            hexsha="d" * 40,
        )
        repo = Mock()
        repo.head.commit = head
        repo.commit.side_effect = lambda ref: {
            "origin/main": main,
            "origin/develop": develop,
        }[ref]
        repo.merge_base.side_effect = [[main_base], [develop_base]]
        repo.is_ancestor.return_value = False
        repo.iter_commits.return_value = [commit]

        with patch("expkits_ci.quality_checks.Repo", return_value=repo):
            result = self.quality_checks.check_commit_messages_on_ci(target_branch="main")

        self.assertTrue(result)
        repo.is_ancestor.assert_called_once_with(main_base, develop_base)
        repo.iter_commits.assert_called_once_with(f"{main_base.hexsha}..HEAD")

    def test_check_commit_messages_on_ci_uses_target_for_equal_merge_bases(self):
        head = types.SimpleNamespace(hexsha="c" * 40)
        main = types.SimpleNamespace(hexsha="a" * 40)
        develop = types.SimpleNamespace(hexsha="b" * 40)
        shared_base = types.SimpleNamespace(hexsha="e" * 40)
        commit = types.SimpleNamespace(
            message="Prepare change\n\nTask: EXPKITS-1191\n",
            hexsha="d" * 40,
        )
        repo = Mock()
        repo.head.commit = head
        repo.commit.side_effect = lambda ref: {
            "origin/develop": develop,
            "origin/main": main,
        }[ref]
        repo.merge_base.side_effect = [[shared_base], [shared_base]]
        repo.iter_commits.return_value = [commit]

        with patch("expkits_ci.quality_checks.Repo", return_value=repo):
            result = self.quality_checks.check_commit_messages_on_ci(target_branch="develop")

        self.assertTrue(result)
        repo.git.fetch.assert_called_once_with(
            "origin",
            "+refs/heads/main:refs/remotes/origin/main",
        )
        repo.is_ancestor.assert_not_called()
        repo.iter_commits.assert_called_once_with(f"{shared_base.hexsha}..HEAD")

    def test_render_commit_message_for_log_falls_back_to_raw_message(self):
        rendered = self.quality_checks.render_commit_message_for_log(
            "# template hint\n# another hint\n",
            [],
        )

        self.assertEqual(rendered, "# template hint\n# another hint")


if __name__ == "__main__":
    unittest.main()
