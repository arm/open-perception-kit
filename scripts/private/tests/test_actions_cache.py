################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import unittest

from scripts.private.actions_cache import superseded


class ActionsCacheTests(unittest.TestCase):
    def test_only_rotates_lanes_with_a_current_replacement(self):
        current, old = "a" * 40, "b" * 40
        caches = [
            {"id": 1, "key": f"pek-ccache-v2-Linux-X64-quality-{old}"},
            {"id": 2, "key": f"pek-ccache-v2-Linux-X64-quality-{current}"},
            {"id": 3, "key": f"pek-ccache-v2-Linux-X64-sonar-{old}"},
            {"id": 4, "key": f"buildkit-Linux-X64-{old}"},
        ]

        self.assertEqual([caches[0]], superseded(caches, current))


if __name__ == "__main__":
    unittest.main()
