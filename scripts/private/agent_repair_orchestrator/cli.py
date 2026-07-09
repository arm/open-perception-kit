#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

if __package__ in (None, ""):  # pragma: no cover - used for direct script execution.
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    __package__ = "agent_repair_orchestrator"

from .paths import default_profile_path_argument
from .repair import (
    command_apply_repair_changes_and_push,
    command_build_markdown,
    command_collect_context,
    command_create_draft_pr,
    command_package_repository_changes,
    command_require_generated_changes,
    command_resolve_inputs,
    command_run_validation,
)


DEFAULT_CONTEXT_ROOT = ".agent-runtime/source-run-repair"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Agent source-run repair helper utility.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    resolve_inputs = subparsers.add_parser("resolve-inputs")
    resolve_inputs.add_argument("--profile-path", default=default_profile_path_argument())
    resolve_inputs.add_argument("--source-run-id", default="")
    resolve_inputs.add_argument("--target-branch", default="")
    resolve_inputs.add_argument("--task-ref", default="")
    resolve_inputs.add_argument("--current-ref-name", default="")
    resolve_inputs.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT", ""))
    resolve_inputs.set_defaults(func=command_resolve_inputs)

    collect_context = subparsers.add_parser("collect-context")
    collect_context.add_argument("--context-root", default=DEFAULT_CONTEXT_ROOT)
    collect_context.add_argument("--source-run-id", required=True)
    collect_context.add_argument("--source-run-url", required=True)
    collect_context.add_argument("--source-workflow-name", required=True)
    collect_context.set_defaults(func=command_collect_context)

    build_markdown = subparsers.add_parser("build-markdown")
    build_markdown.add_argument("--profile-path", default=default_profile_path_argument())
    build_markdown.add_argument("--context-root", default=DEFAULT_CONTEXT_ROOT)
    build_markdown.add_argument("--source-run-id", required=True)
    build_markdown.add_argument("--source-pr-number", default="")
    build_markdown.add_argument("--source-run-url", required=True)
    build_markdown.add_argument("--source-workflow-name", required=True)
    build_markdown.add_argument("--target-branch", required=True)
    build_markdown.add_argument("--repair-branch", required=True)
    build_markdown.add_argument("--task-ref", default="")
    build_markdown.set_defaults(func=command_build_markdown)

    package_changes = subparsers.add_parser("package-repository-changes")
    package_changes.add_argument("--patch-file", required=True)
    package_changes.add_argument("--diffstat-file", required=True)
    package_changes.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT", ""))
    package_changes.set_defaults(func=command_package_repository_changes)

    require_generated_changes = subparsers.add_parser("require-generated-changes")
    require_generated_changes.add_argument("--source-run-id", default="")
    require_generated_changes.set_defaults(func=command_require_generated_changes)

    run_validation = subparsers.add_parser("run-validation")
    run_validation.add_argument("--profile-path", default=default_profile_path_argument())
    run_validation.set_defaults(func=command_run_validation)

    apply_repair_changes = subparsers.add_parser("apply-repair-changes-and-push")
    apply_repair_changes.add_argument("--profile-path", default=default_profile_path_argument())
    apply_repair_changes.add_argument("--patch-root", required=True)
    apply_repair_changes.add_argument("--repair-branch", required=True)
    apply_repair_changes.add_argument("--target-branch", required=True)
    apply_repair_changes.add_argument("--source-run-id", required=True)
    apply_repair_changes.add_argument("--source-pr-number", default="")
    apply_repair_changes.add_argument("--source-run-url", required=True)
    apply_repair_changes.add_argument("--source-workflow-name", required=True)
    apply_repair_changes.add_argument("--task-ref", default="")
    apply_repair_changes.add_argument("--body-file", required=True)
    apply_repair_changes.add_argument("--pr-title-file", required=True)
    apply_repair_changes.add_argument("--commit-subject-file", required=True)
    apply_repair_changes.add_argument("--commit-notes-file", required=True)
    apply_repair_changes.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT", ""))
    apply_repair_changes.set_defaults(func=command_apply_repair_changes_and_push)

    create_draft_pr = subparsers.add_parser("create-draft-pr")
    create_draft_pr.add_argument("--profile-path", default=default_profile_path_argument())
    create_draft_pr.add_argument("--body-file", required=True)
    create_draft_pr.add_argument("--pr-title-file", required=True)
    create_draft_pr.add_argument("--target-branch", required=True)
    create_draft_pr.add_argument("--repair-branch", required=True)
    create_draft_pr.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT", ""))
    create_draft_pr.set_defaults(func=command_create_draft_pr)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return int(args.func(args))
    except subprocess.CalledProcessError as exc:
        if exc.stdout:
            sys.stdout.write(exc.stdout)
        if exc.stderr:
            sys.stderr.write(exc.stderr)
        return exc.returncode
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError, re.error) as exc:
        print(str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
