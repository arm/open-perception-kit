#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import importlib
import sys
import types
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


class DummyRepo:
    def __init__(self, *args, **kwargs):
        self.working_tree_dir = str(Path(__file__).resolve().parents[3])


def import_expkits_ci_module():
    try:
        return importlib.import_module("expkits_ci.expkits_ci")
    except ModuleNotFoundError:
        stale_modules = {
            module_name: sys.modules.pop(module_name, None)
            for module_name in (
                "expkits_ci",
                "expkits_ci.expkits_ci",
                "expkits_ci.expkits_log",
                "expkits_ci.quality_checks",
                "expkits_ci.file_utils",
                "expkits_ci.license_template_manager",
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
                return importlib.import_module("expkits_ci.expkits_ci")
        finally:
            for module_name in (
                "expkits_ci.license_template_manager",
                "expkits_ci.file_utils",
                "expkits_ci.quality_checks",
                "expkits_ci.expkits_log",
                "expkits_ci.expkits_ci",
                "expkits_ci",
            ):
                sys.modules.pop(module_name, None)
            for module_name, module in stale_modules.items():
                if module is not None:
                    sys.modules[module_name] = module


expkits_ci_module = import_expkits_ci_module()


class TestExpkitsCiCli(unittest.TestCase):
    def test_default_ignore_folders_include_runtime_subprojects(self):
        parser = expkits_ci_module.argparse.ArgumentParser()
        expkits_ci_module.setup_argument_parser(parser)

        args = parser.parse_args(["--clang-tidy"])

        self.assertIn("development/subprojects", args.ignore_folder)

    def test_main_applies_all_checks_before_running_perform_checks(self):
        checker = Mock()
        checker.file_utils.get_related_files.return_value = []
        checker.autofix_messages = []
        logger = Mock()

        with patch.object(expkits_ci_module, "QualityChecks", return_value=checker), \
                patch.object(expkits_ci_module, "perform_checks", return_value=True) as perform_checks_mock, \
                patch.object(expkits_ci_module, "setup_expkits_logger", return_value=logger), \
                patch.object(expkits_ci_module.argcomplete, "autocomplete", return_value=None), \
                patch.object(sys, "argv", ["expkits-ci", "--all-checks"]):
            result = expkits_ci_module.main()

        self.assertEqual(result, 0)
        checker.file_utils.get_related_files.assert_called_once()
        parsed_args = perform_checks_mock.call_args.args[1]
        self.assertTrue(parsed_args.branch_naming)
        self.assertTrue(parsed_args.commit_msg_ci)
        self.assertTrue(parsed_args.check_secrets)
        self.assertTrue(parsed_args.actionlint)
        self.assertTrue(parsed_args.agent_runtime_static_analysis)
        self.assertTrue(parsed_args.config_schema_check)
        self.assertTrue(parsed_args.commit_diff)

    def test_pre_commit_fix_preset_enables_shared_autofix_bundle(self):
        checker = Mock()
        checker.file_utils.get_related_files.return_value = []
        checker.autofix_messages = []
        logger = Mock()

        with patch.object(expkits_ci_module, "QualityChecks", return_value=checker), \
                patch.object(expkits_ci_module, "perform_checks", return_value=True) as perform_checks_mock, \
                patch.object(expkits_ci_module, "setup_expkits_logger", return_value=logger), \
                patch.object(expkits_ci_module.argcomplete, "autocomplete", return_value=None), \
                patch.object(sys, "argv", ["expkits-ci", "--pre-commit-fix"]):
            result = expkits_ci_module.main()

        self.assertEqual(result, 0)
        parsed_args = perform_checks_mock.call_args.args[1]
        self.assertTrue(parsed_args.clang_format)
        self.assertTrue(parsed_args.python_format)
        self.assertTrue(parsed_args.cmake_format)
        self.assertTrue(parsed_args.shell_format)
        self.assertTrue(parsed_args.license_header)
        self.assertTrue(parsed_args.check_secrets)
        self.assertTrue(parsed_args.actionlint)
        self.assertFalse(parsed_args.config_schema_check)
        self.assertFalse(parsed_args.clang_format_check)
        self.assertFalse(parsed_args.agent_runtime_static_analysis)

    def test_pre_commit_check_preset_enables_shared_check_only_bundle(self):
        checker = Mock()
        checker.file_utils.get_related_files.return_value = []
        checker.autofix_messages = []
        logger = Mock()

        with patch.object(expkits_ci_module, "QualityChecks", return_value=checker), \
                patch.object(expkits_ci_module, "perform_checks", return_value=True) as perform_checks_mock, \
                patch.object(expkits_ci_module, "setup_expkits_logger", return_value=logger), \
                patch.object(expkits_ci_module.argcomplete, "autocomplete", return_value=None), \
                patch.object(sys, "argv", ["expkits-ci", "--pre-commit-check"]):
            result = expkits_ci_module.main()

        self.assertEqual(result, 0)
        parsed_args = perform_checks_mock.call_args.args[1]
        self.assertTrue(parsed_args.clang_format_check)
        self.assertTrue(parsed_args.python_format_check)
        self.assertTrue(parsed_args.cmake_format_check)
        self.assertTrue(parsed_args.shell_format_check)
        self.assertTrue(parsed_args.license_header_check)
        self.assertTrue(parsed_args.check_secrets)
        self.assertTrue(parsed_args.actionlint)
        self.assertFalse(parsed_args.clang_format)
        self.assertFalse(parsed_args.agent_runtime_static_analysis)
        self.assertFalse(parsed_args.config_schema_check)

    def test_ci_pr_checks_preset_enables_pr_gate(self):
        checker = Mock()
        checker.file_utils.get_related_files.return_value = []
        checker.autofix_messages = []
        logger = Mock()

        with patch.object(expkits_ci_module, "QualityChecks", return_value=checker), \
                patch.object(expkits_ci_module, "perform_checks", return_value=True) as perform_checks_mock, \
                patch.object(expkits_ci_module, "setup_expkits_logger", return_value=logger), \
                patch.object(expkits_ci_module.argcomplete, "autocomplete", return_value=None), \
                patch.object(sys, "argv", ["expkits-ci", "--ci-pr-checks"]):
            result = expkits_ci_module.main()

        self.assertEqual(result, 0)
        parsed_args = perform_checks_mock.call_args.args[1]
        self.assertTrue(parsed_args.branch_naming)
        self.assertTrue(parsed_args.commit_msg_ci)
        self.assertTrue(parsed_args.agent_runtime_static_analysis)
        self.assertTrue(parsed_args.clang_format_check)
        self.assertTrue(parsed_args.check_secrets)
        self.assertTrue(parsed_args.actionlint)
        self.assertTrue(parsed_args.config_schema_check)

    def test_ci_full_checks_preset_enables_full_gate(self):
        checker = Mock()
        checker.file_utils.get_related_files.return_value = []
        checker.autofix_messages = []
        logger = Mock()

        with patch.object(expkits_ci_module, "QualityChecks", return_value=checker), \
                patch.object(expkits_ci_module, "perform_checks", return_value=True) as perform_checks_mock, \
                patch.object(expkits_ci_module, "setup_expkits_logger", return_value=logger), \
                patch.object(expkits_ci_module.argcomplete, "autocomplete", return_value=None), \
                patch.object(sys, "argv", ["expkits-ci", "--ci-full-checks"]):
            result = expkits_ci_module.main()

        self.assertEqual(result, 0)
        parsed_args = perform_checks_mock.call_args.args[1]
        self.assertTrue(parsed_args.agent_runtime_static_analysis)
        self.assertTrue(parsed_args.clang_format_check)
        self.assertTrue(parsed_args.check_secrets)
        self.assertTrue(parsed_args.actionlint)
        self.assertFalse(parsed_args.branch_naming)
        self.assertFalse(parsed_args.commit_msg_ci)
        self.assertTrue(parsed_args.config_schema_check)

    def test_perform_checks_records_actionlint_result(self):
        checker = Mock()
        checker.check_github_actions.return_value = False
        args = Mock(
            check_secrets=False,
            branch_naming=False,
            commit_msg=False,
            commit_msg_ci=False,
            clang_format=False,
            clang_format_check=False,
            clang_tidy=False,
            clang_tidy_stats=False,
            python_format=False,
            python_format_check=False,
            cmake_format=False,
            cmake_format_check=False,
            license_header=False,
            license_header_check=False,
            shell_format=False,
            shell_format_check=False,
            actionlint=True,
            agent_runtime_static_analysis=False,
            config_schema_check=False,
        )
        report = expkits_ci_module.ExecutionReport("custom selection", "explicit", 1, ["--actionlint"])

        result = expkits_ci_module.perform_checks(
            checker,
            args,
            [".github/workflows/pek-ci.yml", "README.md"],
            report,
        )

        self.assertFalse(result)
        checker.check_github_actions.assert_called_once_with([
            ".github/workflows/pek-ci.yml",
            "README.md",
        ])
        self.assertEqual(report.check_results[0].name, "actionlint")
        self.assertFalse(report.check_results[0].passed)

    def test_perform_checks_records_config_schema_result(self):
        checker = Mock()
        checker.check_config_schema.return_value = False
        args = Mock(
            check_secrets=False,
            branch_naming=False,
            commit_msg=False,
            commit_msg_ci=False,
            clang_format=False,
            clang_format_check=False,
            clang_tidy=False,
            clang_tidy_stats=False,
            python_format=False,
            python_format_check=False,
            cmake_format=False,
            cmake_format_check=False,
            license_header=False,
            license_header_check=False,
            shell_format=False,
            shell_format_check=False,
            actionlint=False,
            agent_runtime_static_analysis=False,
            config_schema_check=True,
        )
        report = expkits_ci_module.ExecutionReport(
            "custom selection",
            "all tracked git files",
            0,
            ["--config-schema-check"],
        )

        result = expkits_ci_module.perform_checks(checker, args, [], report)

        self.assertFalse(result)
        checker.check_config_schema.assert_called_once_with()
        self.assertEqual(report.check_results[0].name, "config descriptor validation")
        self.assertFalse(report.check_results[0].passed)

    def test_main_returns_one_when_checks_fail_without_autofixes(self):
        checker = Mock()
        checker.file_utils.get_related_files.return_value = ["safe.txt"]
        checker.autofix_messages = []
        logger = Mock()

        with patch.object(expkits_ci_module, "QualityChecks", return_value=checker), \
                patch.object(expkits_ci_module, "perform_checks", return_value=False), \
                patch.object(expkits_ci_module, "setup_expkits_logger", return_value=logger), \
                patch.object(expkits_ci_module.argcomplete, "autocomplete", return_value=None), \
                patch.object(sys, "argv", ["expkits-ci", "--check-secrets", "--list-of-files", "safe.txt"]):
            result = expkits_ci_module.main()

        self.assertEqual(result, 1)
        logged_errors = "\n".join(call.args[0] for call in logger.error.call_args_list)
        self.assertIn("One or more checks failed.", logged_errors)

    def test_main_returns_one_with_autofix_guidance_when_checks_fail_after_file_updates(self):
        checker = Mock()
        checker.file_utils.get_related_files.return_value = ["safe.txt"]
        checker.autofix_messages = ["safe.txt reformatted"]
        logger = Mock()

        with patch.object(expkits_ci_module, "QualityChecks", return_value=checker), \
                patch.object(expkits_ci_module, "perform_checks", return_value=False), \
                patch.object(expkits_ci_module, "setup_expkits_logger", return_value=logger), \
                patch.object(expkits_ci_module.argcomplete, "autocomplete", return_value=None), \
                patch.object(sys, "argv", ["expkits-ci", "--python-format", "--list-of-files", "safe.txt"]):
            result = expkits_ci_module.main()

        self.assertEqual(result, 1)
        logged_errors = "\n".join(call.args[0] for call in logger.error.call_args_list)
        self.assertIn("Repo checks updated files in place.", logged_errors)


if __name__ == "__main__":
    unittest.main()
