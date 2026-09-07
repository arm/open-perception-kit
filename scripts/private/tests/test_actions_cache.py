################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import importlib
from pathlib import Path
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

actions_cache = importlib.import_module("actions_cache")


CURRENT_SHA = "a" * 40
OLD_SHA = "b" * 40


def cache(cache_id: int, key: str) -> dict[str, object]:
    return {"id": cache_id, "key": key}


class ActionsCacheTests(unittest.TestCase):
    def test_superseded_compiler_caches_requires_a_current_lane_replacement(self):
        caches = [
            cache(1, f"pek-ccache-v2-Linux-X64-quality-{OLD_SHA}"),
            cache(2, f"pek-ccache-v2-Linux-X64-quality-{CURRENT_SHA}"),
            cache(3, f"pek-ccache-v2-Linux-X64-sonar-{OLD_SHA}"),
            cache(4, f"buildkit-Linux-X64-{OLD_SHA}"),
        ]

        self.assertEqual(
            actions_cache.superseded_compiler_caches(caches, CURRENT_SHA),
            [caches[0]],
        )

    @mock.patch.object(actions_cache, "delete_caches")
    @mock.patch.object(actions_cache, "list_caches")
    @mock.patch.object(actions_cache, "read_json")
    @mock.patch.object(actions_cache, "github_repository", return_value="arm-debug/amp-dev-forge")
    def test_closed_pull_request_deletes_every_cache(
        self,
        _github_repository: mock.Mock,
        read_json: mock.Mock,
        list_caches: mock.Mock,
        delete_caches: mock.Mock,
    ):
        read_json.return_value = {"state": "closed", "head": {"sha": CURRENT_SHA}}
        list_caches.return_value = [cache(1, "any-key")]

        actions_cache.reconcile_pull_request("408")

        list_caches.assert_called_once_with("refs/pull/408/merge")
        delete_caches.assert_called_once_with(list_caches.return_value, "closed PR")

    @mock.patch.object(actions_cache, "delete_caches")
    @mock.patch.object(actions_cache, "list_caches")
    @mock.patch.object(actions_cache, "read_json")
    @mock.patch.object(actions_cache, "github_repository", return_value="arm-debug/amp-dev-forge")
    def test_branch_reconciliation_uses_live_head_and_raw_cache_ref(
        self,
        _github_repository: mock.Mock,
        read_json: mock.Mock,
        list_caches: mock.Mock,
        delete_caches: mock.Mock,
    ):
        read_json.return_value = {"commit": {"sha": CURRENT_SHA}}
        list_caches.return_value = [
            cache(1, f"pek-ccache-v2-Linux-X64-quality-{OLD_SHA}"),
            cache(2, f"pek-ccache-v2-Linux-X64-quality-{CURRENT_SHA}"),
        ]

        actions_cache.reconcile_branch("sandbox/cache-e2e")

        self.assertIn("sandbox%2Fcache-e2e", read_json.call_args.args[0][-1])
        list_caches.assert_called_once_with("refs/heads/sandbox/cache-e2e")
        delete_caches.assert_called_once_with([list_caches.return_value[0]], "superseded")

    @mock.patch.object(actions_cache, "delete_caches")
    @mock.patch.object(actions_cache, "list_caches")
    def test_delete_key_matches_only_the_exact_key(
        self, list_caches: mock.Mock, delete_caches: mock.Mock
    ):
        list_caches.return_value = [cache(1, "keep"), cache(2, "delete")]

        actions_cache.delete_key("refs/heads/develop", "delete")

        delete_caches.assert_called_once_with([list_caches.return_value[1]], "retired key")


if __name__ == "__main__":
    unittest.main()
