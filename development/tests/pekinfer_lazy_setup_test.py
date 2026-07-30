#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


GST_LAUNCH = Path(sys.argv[1]).resolve()
PLUGIN = Path(sys.argv[2]).resolve()


class PekInferLazySetupTest(unittest.TestCase):
    def run_pipeline(self, *, active: bool) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            descriptor = root / "opchain.json"
            descriptor.write_text(
                json.dumps(
                    {
                        "name": "unavailable",
                        "ops": [{"id": "missing/Operation", "attributes": {}}],
                    }
                )
            )
            environment = os.environ.copy()
            environment["GST_PLUGIN_PATH_1_0"] = str(PLUGIN.parent)
            environment["GST_REGISTRY_1_0"] = str(root / "registry.bin")
            return subprocess.run(
                [
                    str(GST_LAUNCH),
                    "-q",
                    "videotestsrc",
                    "num-buffers=1",
                    "!",
                    "video/x-raw,format=BGRA,width=16,height=16",
                    "!",
                    "pekinfer",
                    f"opchain-path={descriptor}",
                    f"active={str(active).lower()}",
                    "!",
                    "fakesink",
                ],
                capture_output=True,
                env=environment,
                text=True,
            )

    def test_active_opchain_still_fails_at_startup(self) -> None:
        result = self.run_pipeline(active=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Failed to setup op-chain", result.stderr)

    def test_inactive_opchain_is_not_configured(self) -> None:
        result = self.run_pipeline(active=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("Failed to setup op-chain", result.stderr)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
