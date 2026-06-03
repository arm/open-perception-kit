#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import argparse
import importlib
import io
import sys
import tempfile
import types
import unittest
from contextlib import redirect_stdout
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
CheckResult = expkits_ci_module.CheckResult
create_execution_report = expkits_ci_module.create_execution_report
describe_file_scope = expkits_ci_module.describe_file_scope
emit_report_lines = expkits_ci_module.emit_report_lines
get_enabled_check_flags = expkits_ci_module.get_enabled_check_flags
needs_related_files = expkits_ci_module.needs_related_files
perform_checks = expkits_ci_module.perform_checks
print_result_summary = expkits_ci_module.print_result_summary
print_run_report = expkits_ci_module.print_run_report
run_check = expkits_ci_module.run_check
setup_all_checks = expkits_ci_module.setup_all_checks
setup_argument_parser = expkits_ci_module.setup_argument_parser
write_report_file = expkits_ci_module.write_report_file
build_detailed_report_lines = expkits_ci_module.build_detailed_report_lines
build_execution_plan_lines = expkits_ci_module.build_execution_plan_lines
build_result_summary_lines = expkits_ci_module.build_result_summary_lines


class TestExpkitsCiCli(unittest.TestCase):
    def make_parser(self):
        parser = argparse.ArgumentParser()
        setup_argument_parser(parser)
        return parser

    def parse_args(self, *args):
        return self.make_parser().parse_args(list(args))

    def test_setup_all_checks_enables_expected_flags(self):
        args = self.parse_args()

        setup_all_checks(args)

        self.assertTrue(args.commit_diff)
        self.assertTrue(args.branch_naming)
        self.assertTrue(args.commit_msg_ci)
        self.assertTrue(args.jira_ticket)
        self.assertTrue(args.clang_format_check)
        self.assertTrue(args.python_format_check)
        self.assertTrue(args.cmake_format_check)
        self.assertTrue(args.shell_format_check)
        self.assertTrue(args.license_header_check)
        self.assertTrue(args.check_secrets)
        self.assertFalse(args.clang_tidy)

    def test_get_enabled_check_flags_prefers_format_modes_over_check_modes(self):
        args = self.parse_args(
            "--check-secrets",
            "--clang-format-check",
            "--clang-format",
            "--python-format-check",
            "--python-format",
            "--cmake-format-check",
            "--cmake-format",
            "--license-header-check",
            "--license-header",
            "--shell-format-check",
            "--shell-format",
        )

        self.assertEqual(
            get_enabled_check_flags(args),
            [
                "--check-secrets",
                "--clang-format",
                "--python-format",
                "--cmake-format",
                "--license-header",
                "--shell-format",
            ],
        )

    def test_needs_related_files_and_describe_file_scope_cover_non_file_and_file_modes(self):
        stats_args = self.parse_args("--clang-tidy-stats", "clang-tidy.log")
        explicit_args = self.parse_args("--list-of-files", "a.py", "b.sh")
        pr_args = self.parse_args("--pr-target-branch", "main")
        diff_args = self.parse_args("--commit-diff")

        self.assertFalse(needs_related_files(stats_args))
        self.assertEqual(describe_file_scope(stats_args), "clang-tidy log statistics (clang-tidy.log)")

        self.assertTrue(needs_related_files(explicit_args))
        self.assertEqual(describe_file_scope(explicit_args), "explicit path list (2 input path(s))")

        self.assertTrue(needs_related_files(pr_args))
        self.assertEqual(describe_file_scope(pr_args), "git diff against origin/main...HEAD")

        self.assertTrue(needs_related_files(diff_args))
        self.assertEqual(describe_file_scope(diff_args), "git index diff against HEAD")

    def test_execution_report_and_rendered_lines_reflect_enabled_checks_status_and_pr_target(self):
        args = self.parse_args("--python-format-check", "--pr-target-branch", "main")
        report = create_execution_report(args, describe_file_scope(args), 7)
        report.check_results = [
            CheckResult("python format", True),
            CheckResult("shell format", False),
        ]

        self.assertEqual(report.overall_status, "NOK")

        plan_lines = build_execution_plan_lines(report)
        summary_lines = build_result_summary_lines(report)
        detailed_lines = build_detailed_report_lines(report)

        self.assertEqual(plan_lines[0], "expkits-ci execution plan:")
        self.assertIn("  PR target branch: main", plan_lines)
        self.assertIn("  enabled checks: --python-format-check", plan_lines)
        self.assertIn("  OK   python format", summary_lines)
        self.assertIn("  NOK  shell format", summary_lines)
        self.assertTrue(summary_lines[-1].endswith("NOK"))
        self.assertIn("execution plan:", detailed_lines)
        self.assertIn("  PR target branch: main", detailed_lines)
        self.assertIn("    - --python-format-check", detailed_lines)
        self.assertIn("check results:", detailed_lines)
        self.assertIn("  NOK  shell format", detailed_lines)
        self.assertEqual(detailed_lines[-1], "overall: NOK")

    def test_emit_report_lines_and_write_report_file_respect_file_output(self):
        report = create_execution_report(self.parse_args("--check-secrets"), "all tracked git files", 1)
        report.check_results = [CheckResult("secrets", True)]

        with tempfile.TemporaryDirectory() as temp_dir:
            log_file = Path(temp_dir) / "logs" / "expkits-ci.log"
            report_file = Path(temp_dir) / "artifacts" / "expkits-ci-report.txt"
            log_file.parent.mkdir(parents=True, exist_ok=True)

            emit_report_lines(["line one", "line two"], log_output="file", log_file=str(log_file))
            write_report_file(report, str(report_file))

            self.assertEqual(log_file.read_text(encoding="utf-8"), "[INFO] line one\n[INFO] line two\n")
            written_report = report_file.read_text(encoding="utf-8")
            self.assertIn("expkits-ci report", written_report)
            self.assertIn("overall: OK", written_report)

    def test_print_helpers_render_to_stdout(self):
        args = self.parse_args("--check-secrets")
        report = create_execution_report(args, "all tracked git files", 3)
        report.check_results = [CheckResult("secrets", True)]

        stdout = io.StringIO()
        with redirect_stdout(stdout):
            print_run_report(report)
            print_result_summary(report)

        rendered = stdout.getvalue()
        self.assertIn("[INFO] expkits-ci execution plan:", rendered)
        self.assertIn("[INFO] expkits-ci result summary:", rendered)
        self.assertIn("[INFO]   OK   secrets", rendered)

    def test_run_check_and_perform_checks_record_results_and_arguments(self):
        args = self.parse_args(
            "--check-secrets",
            "--commit-msg-ci",
            "--pr-target-branch",
            "main",
            "--python-format-check",
            "--list-of-files",
            "python/bad.py",
        )
        report = create_execution_report(args, describe_file_scope(args), 1)
        checker = Mock()
        checker.check_secrets.return_value = True
        checker.check_commit_messages_on_ci.return_value = False
        checker.check_python_format.return_value = True

        result = perform_checks(checker, args, ["python/bad.py"], report)

        self.assertFalse(result)
        checker.check_secrets.assert_called_once_with(["python/bad.py"])
        checker.check_commit_messages_on_ci.assert_called_once_with(["python/bad.py"], target_branch="main")
        checker.check_python_format.assert_called_once_with(["python/bad.py"], format=False, verbose=False)
        self.assertEqual(
            [check_result.name for check_result in report.check_results],
            ["secrets", "commit message (CI)", "python format"],
        )
        self.assertEqual(report.overall_status, "NOK")

        standalone_report = create_execution_report(self.parse_args("--branch-naming"), "all tracked git files", 0)
        standalone_result = run_check(standalone_report, "branch naming", lambda: True)
        self.assertTrue(standalone_result)
        self.assertEqual(standalone_report.check_results[0].name, "branch naming")

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
        self.assertTrue(parsed_args.commit_diff)

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
