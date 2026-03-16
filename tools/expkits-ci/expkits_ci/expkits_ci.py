#!/usr/bin/env python3
# PYTHON_ARGCOMPLETE_OK
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

import sys
import argparse
import argcomplete

from expkits_ci.expkits_log import setup_expkits_logger
from expkits_ci.quality_checks import QualityChecks


def setup_argument_parser(parser):
    """Set up the argument parser with all necessary arguments."""
    check_group = parser.add_argument_group('Check Options', 'Enable or disable specific checks.')
    check_group.add_argument("-bn", "--branch-naming", default=False,
                             action="store_true", help="Run branch naming check.")
    check_group.add_argument("-cm", "--commit-msg", default=False,
                             action="store_true", help="Run commit message check.")
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
    util_group.add_argument("-lof", "--list-of-files", nargs='+', default=[],
                            help="Instead of general run on all files, run on the files in the given folder. This is useful for testing specific files.")
    util_group.add_argument("-if", "--ignore-folder", nargs='+', default=["deps", "development/build", ".git", ],
                            help="List of folders to ignore during checks.")


def setup_all_checks(args):
    """Set up all checks to be run by default."""
    args.commit_diff = True
    args.branch_naming = True
    args.commit_msg = True
    args.jira_ticket = True
    args.clang_format_check = True
    # args.clang_tidy = True # TODO: for now clang-tidy should only be advisory
    args.python_format_check = True
    args.cmake_format_check = True
    args.shell_format_check = True
    args.license_header_check = True


def perform_checks(checker, args, files):
    """Perform the specified checks based on the command line arguments."""
    result = True

    if args.check_secrets:
        result = checker.check_secrets(files) and result
    if args.branch_naming:
        result = checker.check_branch_naming() and result
    if args.commit_msg:
        result = checker.check_commit_message(files) and result
    # if args.jira_ticket:
    #     result = checker.check_jira_ticket() and result
    if args.clang_format or args.clang_format_check:
        result = checker.check_clang_format(files, format=args.clang_format, verbose=args.verbose) and result
    if args.clang_tidy:
        result = checker.check_clang_tidy(files) and result
    if args.python_format or args.python_format_check:
        result = checker.check_python_format(files, format=args.python_format, verbose=args.verbose) and result
    if args.cmake_format or args.cmake_format_check:
        result = checker.check_cmake_format(files, format=args.cmake_format, verbose=args.verbose) and result
    if args.license_header or args.license_header_check:
        result = checker.check_license_header(files, format=args.license_header) and result
    if args.shell_format or args.shell_format_check:
        result = checker.check_shell_format(files, format=args.shell_format) and result

    return result


def main():
    """Main function to run Experience Kit CI checks."""
    parser = argparse.ArgumentParser(description="Experience Kit CI checks")
    setup_argument_parser(parser)
    args = parser.parse_args()
    argcomplete.autocomplete(parser)

    logger = setup_expkits_logger(args.verbose, args.log_output, args.log_file)
    logger.info("Starting Experience Kit CI checks...")
    logger.info(f"Arguments: {args}")

    checker = QualityChecks()
    files = checker.file_utils.get_related_files(
        commit_diff=args.commit_diff,
        pr_target_branch=args.pr_target_branch,
        files=args.list_of_files,
        ignore_folder=args.ignore_folder)

    if not files:
        logger.info("No files found to check.")

    if args.all_checks:
        logger.info("Imitating CI run on all files.")
        setup_all_checks(args)

    result = perform_checks(checker, args, files)
    if not result:
        error_message = "One or more checks failed. Please review the logs for details. Use --verbose for more information."
        logger.error(error_message)
        raise RuntimeError(error_message)

    return not result


if __name__ == "__main__":
    main()
