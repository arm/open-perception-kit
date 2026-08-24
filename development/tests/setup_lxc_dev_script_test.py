#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import re
import sys
import unittest
from pathlib import Path


REPOSITORY_ARGUMENT = Path(sys.argv[1]) if len(sys.argv) > 1 else None
REPOSITORY_ROOT = (
    REPOSITORY_ARGUMENT.resolve()
    if REPOSITORY_ARGUMENT is not None and REPOSITORY_ARGUMENT.is_dir()
    else Path(__file__).resolve().parents[2]
)
SETUP_SCRIPT = REPOSITORY_ROOT / "scripts/setup-lxc-dev.sh"
ENTRYPOINT_PATTERN = re.compile(r"scripts/[A-Za-z0-9_./-]+\.(?:py|sh)")


class SetupLxcDevScriptTests(unittest.TestCase):
    def test_referenced_repository_entrypoints_exist(self) -> None:
        entrypoints = set(ENTRYPOINT_PATTERN.findall(SETUP_SCRIPT.read_text()))
        self.assertTrue(entrypoints)

        missing = sorted(
            entrypoint
            for entrypoint in entrypoints
            if not (REPOSITORY_ROOT / entrypoint).is_file()
        )
        self.assertEqual(missing, [], f"Missing setup entrypoints: {missing}")


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
