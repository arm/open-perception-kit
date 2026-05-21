#!/usr/bin/env python3
# PYTHON_ARGCOMPLETE_OK
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

import os
import sys
import argparse
from dataclasses import dataclass, field
from typing import List, Optional
import argcomplete

from expkits_ci.expkits_log import setup_expkits_logger
from expkits_ci.quality_checks import QualityChecks


@dataclass
class CheckResult:
    name: str
    passed: bool


@dataclass
class ExecutionReport:
    preset: str
    file_scope: str
    resolved_files: int
    enabled_checks: List[str]
    pr_target_branch: Optional[str] = None
    check_results: List[CheckResult] = field(default_factory=list)

    @property
    def overall_status(self):
        if not self.check_results:
            return "OK"
        return "OK" if all(check_result.passed for check_result in self.check_results) else "NOK"


def setup_argument_parser(parser):
    """Set up the argument parser with all necessary arguments."""
    check_group = parser.add_argument_group('Check Options', 'Enable or disable specific checks.')
    check_group.add_argument("-bn", "--branch-naming", default=False,
                             action="store_true", help="Run branch naming check.")
    check_group.add_argument("-cm", "--commit-msg", default=False,
                             action="store_true", help="Run commit message check during committing.")
    check_group.add_argument("-cmci", "--commit-msg-ci", default=False,
                             action="store_true", help="Run commit message check on CI.")
    check_group.add_argument("-jt", "--jira-ticket", default=False, action="store_true", help="Run JIRA ticket check.")
    check_group.add_argument("-clfc", "--clang-format-check", default=False,
                             action="store_true", help="Run clang-format check.")
    check_group.add_argument("-clf", "--clang-format", default=False, action="store_true",
                             help="Run clang-format check and format if needed.")
    check_group.add_argument("-clt", "--clang-tidy", default=False, action="store_true", help="Run clang-tidy check.")
    check_group.add_argument("-pyfc", "--python-format-check", default=False,
                             action="store_true", help="Run PEP-8 check.")
    check_group.add_argument("-pyf", "--python-format", default=False, action="store_true", help="Run PEP-8 format.")
    check_group.add_argument("-cmfc", "--cmake-format-check", default=False,
                             action="store_true", help="Run cmake format check.")
    check_group.add_argument("-cmf", "--cmake-format", default=False, action="store_true", help="Run cmake format.")

    check_group.add_argument("-shfc", "--shell-format-check", default=False,
                             action="store_true", help="Run shell format check.")
    check_group.add_argument("-shf", "--shell-format", default=False, action="store_true", help="Run shell format.")

    check_group.add_argument("-lhc", "--license-header-check", default=False, action="store_true",
                             help="Check for license header in files.")
    check_group.add_argument("-lh", "--license-header", default=False,
                             action="store_true", help="Add license header to files.")

    check_group.add_argument("-sc", "--check-secrets", default=False,
                             action="store_true", help="Check for secrets in files.")

    util_group = parser.add_argument_group('Utility Options', 'General script and logging options.')
    util_group.add_argument("-v", "--verbose", default=False, action="store_true", help="Enable verbose output.")
    util_group.add_argument("-ac", "--all-checks", default=False, action="store_true",
                            help="Run every check on all files with --commit-diff. Imitating of the CI run.")
    util_group.add_argument("-do", "--commit-diff", default=False, action="store_true",
                            help="Run checks only on files changed.")
    util_group.add_argument("-pr", "--pr-target-branch", type=str, default=None,
                            help="Target branch to diff against for PR checks (e.g., main or develop).")
    util_group.add_argument("-lo", "--log-output", choices=["stdout", "file", "both"],
                            default="stdout", help="Log output destination: stdout, file, or both.")
    util_group.add_argument("-lf", "--log-file", default="expkits_ci.log", help="Log file path if logging to file.")
    util_group.add_argument("-rf", "--report-file", default=None,
                            help="Optional plain-text report path with the effective plan and check results.")
    util_group.add_argument("-lof", "--list-of-files", nargs='+', default=[],
                            help="Instead of general run on all files, run on the files in the given folder. This is useful for testing specific files.")
    util_group.add_argument("-if", "--ignore-folder", nargs='+', default=["deps", "development/build", ".git", ],
                            help="List of folders to ignore during checks.")
    util_group.add_argument("--compile-commands-dir", default=None,
                            help="Directory containing compile_commands.json for clang-tidy. Defaults to development/build.")
    util_group.add_argument("--clang-tidy-binary", default=None,
                            help="Path to clang-tidy. Defaults to PATH, then the active Python environment.")
    util_group.add_argument("--clang-tidy-stats", default=None, metavar="LOG_FILE",
                            help="Parse a clang-tidy log file and report diagnostic counts by check name.")


def setup_all_checks(args):
    """Set up all checks to be run by default."""
    args.commit_diff = True
    args.branch_naming = True
    args.commit_msg_ci = True
    args.jira_ticket = True
    args.clang_format_check = True
    # args.clang_tidy = True # TODO: for now clang-tidy should only be advisory
    args.python_format_check = True
    args.cmake_format_check = True
    args.shell_format_check = True
    args.license_header_check = True
    args.check_secrets = True


def get_enabled_check_flags(args):
    """Return the effective check flags that will run in this invocation."""
    enabled_checks = []

    if args.check_secrets:
        enabled_checks.append("--check-secrets")
    if args.branch_naming:
        enabled_checks.append("--branch-naming")
    if args.commit_msg:
        enabled_checks.append("--commit-msg")
    if args.commit_msg_ci:
        enabled_checks.append("--commit-msg-ci")
    if args.clang_format:
        enabled_checks.append("--clang-format")
    elif args.clang_format_check:
        enabled_checks.append("--clang-format-check")
    if args.clang_tidy:
        enabled_checks.append("--clang-tidy")
    if args.clang_tidy_stats:
        enabled_checks.append("--clang-tidy-stats")
    if args.python_format:
        enabled_checks.append("--python-format")
    elif args.python_format_check:
        enabled_checks.append("--python-format-check")
    if args.cmake_format:
        enabled_checks.append("--cmake-format")
    elif args.cmake_format_check:
        enabled_checks.append("--cmake-format-check")
    if args.license_header:
        enabled_checks.append("--license-header")
    elif args.license_header_check:
        enabled_checks.append("--license-header-check")
    if args.shell_format:
        enabled_checks.append("--shell-format")
    elif args.shell_format_check:
        enabled_checks.append("--shell-format-check")

    return enabled_checks


def needs_related_files(args):
    """Return True when enabled checks need precomputed file lists."""
    file_based_check_enabled = any([
        args.check_secrets,
        args.commit_msg,
        args.commit_msg_ci,
        args.clang_format,
        args.clang_format_check,
        args.clang_tidy,
        args.python_format,
        args.python_format_check,
        args.cmake_format,
        args.cmake_format_check,
        args.shell_format,
        args.shell_format_check,
        args.license_header,
        args.license_header_check,
        args.all_checks,
    ])
    return file_based_check_enabled or bool(args.list_of_files) or args.commit_diff or args.pr_target_branch


def describe_file_scope(args):
    """Describe how the file set will be resolved for the current run."""
    if args.clang_tidy_stats and not needs_related_files(args):
        return f"clang-tidy log statistics ({args.clang_tidy_stats})"
    if args.list_of_files:
        return f"explicit path list ({len(args.list_of_files)} input path(s))"
    if args.pr_target_branch:
        return f"git diff against origin/{args.pr_target_branch}...HEAD"
    if args.commit_diff:
        return "git index diff against HEAD"
    return "all tracked git files"


def create_execution_report(args, file_scope, file_count):
    """Create the canonical run report model for this invocation."""
    return ExecutionReport(
        preset="--all-checks" if args.all_checks else "custom selection",
        file_scope=file_scope,
        resolved_files=file_count,
        enabled_checks=get_enabled_check_flags(args),
        pr_target_branch=args.pr_target_branch,
    )


def build_execution_plan_lines(report):
    """Render the always-visible execution plan section."""
    plan_lines = [
        "expkits-ci execution plan:",
        f"  preset: {report.preset}",
        f"  file scope: {report.file_scope}",
        f"  resolved files: {report.resolved_files}",
        f"  enabled checks: {', '.join(report.enabled_checks) if report.enabled_checks else 'none'}",
    ]

    if report.pr_target_branch:
        plan_lines.insert(3, f"  PR target branch: {report.pr_target_branch}")

    return plan_lines


def build_result_summary_lines(report):
    """Render the concise end-of-run console summary."""
    summary_lines = ["expkits-ci result summary:"]

    if not report.check_results:
        summary_lines.append("  OK   no checks were selected")
        return summary_lines

    for check_result in report.check_results:
        status = "OK" if check_result.passed else "NOK"
        summary_lines.append(f"  {status:<3}  {check_result.name}")

    summary_lines.append(f"  {report.overall_status}")
    return summary_lines


def build_detailed_report_lines(report):
    """Render the full plain-text report artifact."""
    report_lines = [
        "expkits-ci report",
        "",
        "execution plan:",
        f"  preset: {report.preset}",
        f"  file scope: {report.file_scope}",
        f"  resolved files: {report.resolved_files}",
    ]

    if report.pr_target_branch:
        report_lines.append(f"  PR target branch: {report.pr_target_branch}")

    report_lines.extend([
        "  enabled checks:",
    ])

    if report.enabled_checks:
        report_lines.extend(f"    - {check_flag}" for check_flag in report.enabled_checks)
    else:
        report_lines.append("    - none")

    report_lines.extend(["", "check results:"])

    if report.check_results:
        for check_result in report.check_results:
            status = "OK" if check_result.passed else "NOK"
            report_lines.append(f"  {status:<3}  {check_result.name}")
    else:
        report_lines.append("  OK   no checks were selected")

    report_lines.extend(["", f"overall: {report.overall_status}"])
    return report_lines


def emit_report_lines(report_lines, log_output="stdout", log_file="expkits_ci.log"):
    """Emit report lines while respecting the configured log routing."""
    formatted_lines = [f"[INFO] {line}\n" for line in report_lines]

    if log_output in ("stdout", "both"):
        for line in formatted_lines:
            print(line, end="", flush=True)

    if log_output in ("file", "both"):
        with open(log_file, "a", encoding="utf-8") as log_handle:
            log_handle.writelines(formatted_lines)


def write_report_file(report, report_file):
    """Write the full plain-text report artifact when requested."""
    if not report_file:
        return

    report_dir = os.path.dirname(report_file)
    if report_dir:
        os.makedirs(report_dir, exist_ok=True)

    with open(report_file, "w", encoding="utf-8") as report_handle:
        for line in build_detailed_report_lines(report):
            report_handle.write(f"{line}\n")


def print_run_report(report, log_output="stdout", log_file="expkits_ci.log"):
    """Print a concise execution plan that is always visible on stdout."""
    emit_report_lines(build_execution_plan_lines(report), log_output, log_file)


def print_result_summary(report, log_output="stdout", log_file="expkits_ci.log"):
    """Print a concise end-of-run summary that is always visible on stdout."""
    emit_report_lines(build_result_summary_lines(report), log_output, log_file)


def run_check(report, check_name, check_fn):
    """Run a single check, record its result, and return the boolean outcome."""
    result = check_fn()
    report.check_results.append(CheckResult(check_name, result))
    return result


def perform_checks(checker, args, files, report):
    """Perform the specified checks based on the command line arguments."""
    result = True

    if args.check_secrets:
        result = run_check(report, "secrets", lambda: checker.check_secrets(files)) and result
    if args.branch_naming:
        result = run_check(report, "branch naming", checker.check_branch_naming) and result
    if args.commit_msg:
        result = run_check(report, "commit message", lambda: checker.check_commit_message(files)) and result
    if args.commit_msg_ci:
        result = run_check(
            report,
            "commit message (CI)",
            lambda: checker.check_commit_messages_on_ci(files, target_branch=args.pr_target_branch),
        ) and result
    # if args.jira_ticket:
    #     result = checker.check_jira_ticket() and result
    if args.clang_format or args.clang_format_check:
        result = run_check(
            report,
            "clang-format",
            lambda: checker.check_clang_format(files, format=args.clang_format, verbose=args.verbose),
        ) and result
    if args.clang_tidy:
        result = run_check(
            report,
            "clang-tidy",
            lambda: checker.check_clang_tidy(
                files,
                compile_commands_dir=args.compile_commands_dir,
                clang_tidy_binary=args.clang_tidy_binary),
        ) and result
    if args.clang_tidy_stats:
        result = run_check(
            report,
            "clang-tidy stats",
            lambda: checker.report_clang_tidy_statistics(args.clang_tidy_stats),
        ) and result
    if args.python_format or args.python_format_check:
        result = run_check(
            report,
            "python format",
            lambda: checker.check_python_format(files, format=args.python_format, verbose=args.verbose),
        ) and result
    if args.cmake_format or args.cmake_format_check:
        result = run_check(
            report,
            "cmake format",
            lambda: checker.check_cmake_format(files, format=args.cmake_format, verbose=args.verbose),
        ) and result
    if args.license_header or args.license_header_check:
        result = run_check(
            report,
            "license header",
            lambda: checker.check_license_header(files, format=args.license_header),
        ) and result
    if args.shell_format or args.shell_format_check:
        result = run_check(
            report,
            "shell format",
            lambda: checker.check_shell_format(files, format=args.shell_format),
        ) and result

    return result


def main():
    """Main function to run Experience Kit CI checks."""
    parser = argparse.ArgumentParser(description="Experience Kit CI checks")
    setup_argument_parser(parser)
    args = parser.parse_args()
    argcomplete.autocomplete(parser)

    if args.clang_tidy_stats:
        args.verbose = True

    logger = setup_expkits_logger(args.verbose, args.log_output, args.log_file)
    logger.info("Starting Experience Kit CI checks...")
    logger.info(f"Arguments: {args}")

    if args.all_checks:
        logger.info("Applying --all-checks preset.")
        setup_all_checks(args)

    file_scope = describe_file_scope(args)
    checker = QualityChecks()
    needs_files = needs_related_files(args)
    if needs_files:
        files = checker.file_utils.get_related_files(
            commit_diff=args.commit_diff,
            pr_target_branch=args.pr_target_branch,
            files=args.list_of_files,
            ignore_folder=args.ignore_folder)
    else:
        files = []

    report = create_execution_report(args, file_scope, len(files))
    print_run_report(report, args.log_output, args.log_file)

    if needs_files and not files:
        logger.info("No files found to check.")

    result = perform_checks(checker, args, files, report)
    print_result_summary(report, args.log_output, args.log_file)
    write_report_file(report, args.report_file)

    if not result:
        error_message = "One or more checks failed. Please review the logs for details. Use --verbose for more information."
        logger.error(error_message)
        raise RuntimeError(error_message)

    return not result


if __name__ == "__main__":
    main()
