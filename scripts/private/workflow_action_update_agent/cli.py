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
    __package__ = "workflow_action_update_agent"

from .repair import (
    command_apply_repair_changes_and_push,
    command_build_markdown,
    command_collect_context,
    command_create_draft_pr,
    command_package_repository_changes,
    command_require_generated_changes,
    command_resolve_inputs,
)
from .runtime import default_profile_path_argument
from .stabilization import (
    command_commit_review_fix,
    command_prepare_stabilization_context,
    command_resolve_pr_details,
    command_restore_helper_bundle,
    command_run_validation,
    command_snapshot_helper_bundle,
    command_stabilize_pr,
)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Workflow Action Update Agent helper utility.")
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
    collect_context.add_argument("--context-root", default=".agent-runtime/workflow-action-update-agent")
    collect_context.add_argument("--source-run-id", required=True)
    collect_context.add_argument("--source-run-url", required=True)
    collect_context.add_argument("--source-workflow-name", required=True)
    collect_context.set_defaults(func=command_collect_context)

    build_markdown = subparsers.add_parser("build-markdown")
    build_markdown.add_argument("--profile-path", default=default_profile_path_argument())
    build_markdown.add_argument("--context-root", default=".agent-runtime/workflow-action-update-agent")
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

    resolve_pr_details = subparsers.add_parser("resolve-pr-details")
    resolve_pr_details.add_argument("--pr-number", required=True)
    resolve_pr_details.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT", ""))
    resolve_pr_details.set_defaults(func=command_resolve_pr_details)

    snapshot_helper_bundle = subparsers.add_parser("snapshot-helper-bundle")
    snapshot_helper_bundle.add_argument("--bundle-root", required=True)
    snapshot_helper_bundle.set_defaults(func=command_snapshot_helper_bundle)

    restore_helper_bundle = subparsers.add_parser("restore-helper-bundle")
    restore_helper_bundle.add_argument("--bundle-root", required=True)
    restore_helper_bundle.add_argument("--helper-root", default=".workflow-action-update-agent-helper")
    restore_helper_bundle.set_defaults(func=command_restore_helper_bundle)

    prepare_stabilization_context = subparsers.add_parser("prepare-stabilization-context")
    prepare_stabilization_context.add_argument(
        "--profile-path",
        default=default_profile_path_argument(),
    )
    prepare_stabilization_context.add_argument("--pr-number", required=True)
    prepare_stabilization_context.add_argument("--head-sha", default="")
    prepare_stabilization_context.add_argument("--source-run-id", default="")
    prepare_stabilization_context.add_argument(
        "--context-root",
        default=".agent-runtime/workflow-action-update-agent",
    )
    prepare_stabilization_context.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT", ""))
    prepare_stabilization_context.set_defaults(func=command_prepare_stabilization_context)

    run_validation = subparsers.add_parser("run-validation")
    run_validation.add_argument("--profile-path", default=default_profile_path_argument())
    run_validation.set_defaults(func=command_run_validation)

    commit_review_fix_parser = subparsers.add_parser("commit-review-fix")
    commit_review_fix_parser.add_argument("--context-root", default=".agent-runtime/workflow-action-update-agent")
    commit_review_fix_parser.add_argument("--pr-number", required=True)
    commit_review_fix_parser.add_argument("--repair-branch", required=True)
    commit_review_fix_parser.add_argument("--task-ref", default="")
    commit_review_fix_parser.add_argument("--github-output", default=os.environ.get("GITHUB_OUTPUT", ""))
    commit_review_fix_parser.set_defaults(func=command_commit_review_fix)

    stabilize_pr = subparsers.add_parser("stabilize-pr")
    stabilize_pr.add_argument("--profile-path", default=default_profile_path_argument())
    stabilize_pr.add_argument("--pr-number", required=True)
    stabilize_pr.add_argument("--repair-branch", required=True)
    stabilize_pr.add_argument("--head-sha", required=True)
    stabilize_pr.add_argument("--task-ref", default="")
    stabilize_pr.add_argument("--source-run-id", required=True)
    stabilize_pr.add_argument("--context-root", default=".agent-runtime/workflow-action-update-agent")
    stabilize_pr.add_argument("--merge-when-stable", action="store_true")
    stabilize_pr.set_defaults(func=command_stabilize_pr)

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
