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
    __package__ = "agent_stabilization_orchestrator"

from .paths import default_profile_path_argument
from .stabilization import (
    command_commit_review_fix,
    command_prepare_stabilization_context,
    command_resolve_pr_details,
    command_restore_helper_bundle,
    command_run_validation,
    command_snapshot_helper_bundle,
)


DEFAULT_CONTEXT_ROOT = ".agent-runtime/pr-stabilization"
DEFAULT_HELPER_ROOT = ".agent-runtime/agent-stabilization-helper"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Agent PR stabilization helper utility.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    resolve_pr_details = subparsers.add_parser("resolve-pr-details")
    resolve_pr_details.add_argument("--pr-number", required=True)
    resolve_pr_details.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT", ""))
    resolve_pr_details.set_defaults(func=command_resolve_pr_details)

    snapshot_helper_bundle = subparsers.add_parser("snapshot-helper-bundle")
    snapshot_helper_bundle.add_argument("--bundle-root", required=True)
    snapshot_helper_bundle.set_defaults(func=command_snapshot_helper_bundle)

    restore_helper_bundle = subparsers.add_parser("restore-helper-bundle")
    restore_helper_bundle.add_argument("--bundle-root", required=True)
    restore_helper_bundle.add_argument("--helper-root", default=DEFAULT_HELPER_ROOT)
    restore_helper_bundle.set_defaults(func=command_restore_helper_bundle)

    prepare_stabilization_context = subparsers.add_parser("prepare-stabilization-context")
    prepare_stabilization_context.add_argument("--profile-path", default=default_profile_path_argument())
    prepare_stabilization_context.add_argument("--pr-number", required=True)
    prepare_stabilization_context.add_argument("--head-sha", default="")
    prepare_stabilization_context.add_argument("--source-run-id", default="")
    prepare_stabilization_context.add_argument("--context-root", default=DEFAULT_CONTEXT_ROOT)
    prepare_stabilization_context.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT", ""))
    prepare_stabilization_context.set_defaults(func=command_prepare_stabilization_context)

    run_validation = subparsers.add_parser("run-validation")
    run_validation.add_argument("--profile-path", default=default_profile_path_argument())
    run_validation.set_defaults(func=command_run_validation)

    commit_review_fix_parser = subparsers.add_parser("commit-review-fix")
    commit_review_fix_parser.add_argument("--context-root", default=DEFAULT_CONTEXT_ROOT)
    commit_review_fix_parser.add_argument("--pr-number", required=True)
    commit_review_fix_parser.add_argument("--head-branch", required=True)
    commit_review_fix_parser.add_argument("--task-ref", default="")
    commit_review_fix_parser.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT", ""))
    commit_review_fix_parser.set_defaults(func=command_commit_review_fix)

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
