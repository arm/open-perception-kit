#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates

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
