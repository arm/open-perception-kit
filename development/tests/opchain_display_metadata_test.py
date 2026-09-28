#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

import json
import pathlib
import sys
import unittest


class OpChainDisplayMetadataTest(unittest.TestCase):
    def test_all_checked_in_opchains_define_display_metadata(self):
        repository = pathlib.Path(sys.argv[1])
        descriptors = sorted(
            list((repository / "config" / "models").rglob("opchain*.json"))
            + list((repository / "config" / "opchains").rglob("opchain*.json"))
        )
        self.assertTrue(descriptors, "No OpChain descriptors found")

        required_fields = ("displayName", "task", "runtime")
        invalid = []
        for descriptor in descriptors:
            with descriptor.open(encoding="utf-8") as descriptor_file:
                data = json.load(descriptor_file)
            missing = [
                field
                for field in required_fields
                if not isinstance(data.get(field), str) or not data[field].strip()
            ]
            if missing:
                invalid.append(f"{descriptor.relative_to(repository)}: {', '.join(missing)}")

        self.assertEqual(invalid, [], "Invalid display metadata:\n" + "\n".join(invalid))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
