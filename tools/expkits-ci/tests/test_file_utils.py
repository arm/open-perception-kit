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


def import_file_utils_module():
    try:
        return importlib.import_module("expkits_ci.file_utils")
    except ModuleNotFoundError:
        stale_modules = {
            module_name: sys.modules.pop(module_name, None)
            for module_name in ("expkits_ci", "expkits_ci.expkits_ci", "expkits_ci.file_utils")
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
                return importlib.import_module("expkits_ci.file_utils")
        finally:
            for module_name in ("expkits_ci.file_utils", "expkits_ci.expkits_ci", "expkits_ci"):
                sys.modules.pop(module_name, None)
            for module_name, module in stale_modules.items():
                if module is not None:
                    sys.modules[module_name] = module


file_utils_module = import_file_utils_module()
FileUtils = file_utils_module.FileUtils


class TestFileUtils(unittest.TestCase):
    def test_resolve_target_branch_ref_prefers_existing_origin_ref(self):
        repo = Mock()
        repo.git.fetch = Mock()

        def commit_side_effect(ref_name):
            if ref_name == "origin/main":
                return object()
            raise ValueError(f"unknown ref: {ref_name}")

        repo.commit.side_effect = commit_side_effect

        self.assertEqual(FileUtils.resolve_target_branch_ref(repo, "main"), "origin/main")
        repo.git.fetch.assert_not_called()

    def test_resolve_target_branch_ref_falls_back_to_local_branch_without_fetch(self):
        repo = Mock()
        repo.git.fetch = Mock()

        def commit_side_effect(ref_name):
            if ref_name == "main":
                return object()
            raise ValueError(f"unknown ref: {ref_name}")

        repo.commit.side_effect = commit_side_effect

        self.assertEqual(FileUtils.resolve_target_branch_ref(repo, "main"), "main")
        repo.git.fetch.assert_not_called()

    def test_resolve_target_branch_ref_fetches_only_when_no_ref_exists(self):
        repo = Mock()
        repo.git.fetch = Mock()
        state = {"fetched": False}

        def commit_side_effect(ref_name):
            if ref_name == "origin/main" and state["fetched"]:
                return object()
            raise ValueError(f"unknown ref: {ref_name}")

        def fetch_side_effect(remote_name, branch_name):
            self.assertEqual(remote_name, "origin")
            self.assertEqual(branch_name, "main")
            state["fetched"] = True

        repo.commit.side_effect = commit_side_effect
        repo.git.fetch.side_effect = fetch_side_effect

        self.assertEqual(FileUtils.resolve_target_branch_ref(repo, "main"), "origin/main")
        repo.git.fetch.assert_called_once_with("origin", "main")

    def test_resolve_target_branch_ref_propagates_unexpected_probe_errors(self):
        repo = Mock()
        repo.git.fetch = Mock()
        repo.commit.side_effect = RuntimeError("repo broken")

        with self.assertRaises(RuntimeError):
            FileUtils.resolve_target_branch_ref(repo, "main")

        repo.git.fetch.assert_not_called()


if __name__ == "__main__":
    unittest.main()
