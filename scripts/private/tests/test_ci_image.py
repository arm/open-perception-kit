################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import importlib
from datetime import datetime, timezone
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

ci_image = importlib.import_module("ci_image")


HEAD_SHA = "2" * 40
BASE_SHA = "3" * 40


class CiImageTests(unittest.TestCase):
    def test_retention_deletes_untagged_and_old_sha_only_versions(self):
        versions = [
            {
                "id": 1,
                "created_at": "2026-01-04",
                "metadata": {"container": {"tags": [f"sha-{HEAD_SHA}"]}},
            },
            {"id": 2, "created_at": "2026-01-03", "metadata": {"container": {"tags": ["buildcache"]}}},
            {
                "id": 3,
                "created_at": "2026-01-02",
                "metadata": {"container": {"tags": [f"sha-{BASE_SHA}"]}},
            },
            {
                "id": 4,
                "created_at": "2026-01-01",
                "metadata": {"container": {"tags": ["sha-protected"]}},
            },
            {"id": 5, "created_at": "2026-01-05", "metadata": {"container": {"tags": []}}},
        ]
        self.assertEqual(ci_image.versions_to_delete(versions, keep=1), [3, 5])

    def test_ci_retention_deletes_only_stale_ci_versions(self):
        versions = [
            {
                "id": 1,
                "created_at": "2026-01-01T00:00:00Z",
                "metadata": {"container": {"tags": ["pek-ci-run-101"]}},
            },
            {
                "id": 2,
                "created_at": "2026-01-04T00:00:00Z",
                "metadata": {"container": {"tags": ["pek-ci-run-102"]}},
            },
            {
                "id": 3,
                "created_at": "2026-01-01T00:00:00Z",
                "metadata": {
                    "container": {"tags": ["pek-ci-run-103", "pek-ci-pr-378"]}
                },
            },
            {
                "id": 4,
                "created_at": "2026-01-01T00:00:00Z",
                "metadata": {"container": {"tags": ["buildcache"]}},
            },
            {
                "id": 5,
                "created_at": "2026-01-01T00:00:00Z",
                "metadata": {"container": {"tags": []}},
            },
        ]
        cutoff = datetime(2026, 1, 3, tzinfo=timezone.utc)

        self.assertEqual(ci_image.stale_ci_versions_to_delete(versions, cutoff), [1, 3, 5])


if __name__ == "__main__":
    unittest.main()
