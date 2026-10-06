# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import unittest

from scripts.private.actions_cache import superseded


class ActionsCacheTests(unittest.TestCase):
    def test_only_rotates_lanes_with_a_current_replacement(self):
        current, old = "a" * 40, "b" * 40
        caches = [
            {"id": 1, "key": f"opk-ccache-v2-Linux-X64-quality-{old}"},
            {"id": 2, "key": f"opk-ccache-v2-Linux-X64-quality-{current}"},
            {"id": 3, "key": f"opk-ccache-v2-Linux-X64-sonar-{old}"},
            {"id": 4, "key": f"buildkit-Linux-X64-{old}"},
        ]

        self.assertEqual([caches[0]], superseded(caches, current))


if __name__ == "__main__":
    unittest.main()
