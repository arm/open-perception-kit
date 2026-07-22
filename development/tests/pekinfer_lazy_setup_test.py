#!/usr/bin/env python3
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

"""Exercise pekinfer's activation boundary through the built GStreamer plugin."""

from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

GST_LAUNCH = Path(sys.argv[1]).resolve()
PLUGIN_PATH = Path(sys.argv[2]).resolve()
BLOCKING_SETUP_OP_PATH = Path(sys.argv[3]).resolve()


class PekInferLazySetupTest(unittest.TestCase):
    @staticmethod
    def pipeline_environment(directory: Path) -> dict[str, str]:
        environment = os.environ.copy()
        environment["GST_PLUGIN_PATH_1_0"] = str(PLUGIN_PATH.parent)
        environment["GST_REGISTRY_1_0"] = str(directory / "registry.bin")
        existing_library_path = environment.get("LD_LIBRARY_PATH")
        environment["LD_LIBRARY_PATH"] = str(BLOCKING_SETUP_OP_PATH.parent)
        if existing_library_path:
            environment["LD_LIBRARY_PATH"] += os.pathsep + existing_library_path
        return environment

    def run_pipeline(self, *, active: bool) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory(prefix="pekinfer-lazy-setup-") as directory:
            test_directory = Path(directory)
            descriptor = test_directory / "opchain.json"
            descriptor.write_text(
                json.dumps(
                    {
                        "name": "deferred-setup",
                        "ops": [{"id": "missing/Operation", "attributes": {}}],
                    }
                ),
                encoding="utf-8",
            )
            return subprocess.run(
                [
                    str(GST_LAUNCH),
                    "-q",
                    "videotestsrc",
                    f"num-buffers={15 if active else 1}",
                    f"is-live={'true' if active else 'false'}",
                    "!",
                    "video/x-raw,format=BGRA,width=16,height=16",
                    "!",
                    "pekinfer",
                    f"opchain-path={descriptor}",
                    f"active={'true' if active else 'false'}",
                    "!",
                    "fakesink",
                ],
                check=False,
                capture_output=True,
                env=self.pipeline_environment(test_directory),
                text=True,
            )

    def test_inactive_opchain_does_not_setup_operations(self) -> None:
        result = self.run_pipeline(active=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("Asynchronous OpChain setup failed", result.stderr)

    def test_active_setup_failure_keeps_pipeline_running(self) -> None:
        result = self.run_pipeline(active=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Asynchronous OpChain setup failed", result.stderr)

    def test_blocking_setup_does_not_block_the_streaming_thread(self) -> None:
        with tempfile.TemporaryDirectory(prefix="pekinfer-async-setup-") as directory:
            test_directory = Path(directory)
            descriptor = test_directory / "opchain.json"
            descriptor.write_text(
                json.dumps(
                    {
                        "name": "blocking-setup",
                        "ops": [
                            {
                                "id": "pek-test-blocking-setup/BlockingSetup",
                                "attributes": {},
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            started = test_directory / "setup-started"
            release = test_directory / "setup-release"
            output = test_directory / "frame.raw"
            environment = self.pipeline_environment(test_directory)
            environment["PEK_TEST_BLOCKING_SETUP_STARTED"] = str(started)
            environment["PEK_TEST_BLOCKING_SETUP_RELEASE"] = str(release)
            process = subprocess.Popen(
                [
                    str(GST_LAUNCH),
                    "-q",
                    "videotestsrc",
                    "num-buffers=15",
                    "is-live=true",
                    "!",
                    "video/x-raw,format=BGRA,width=16,height=16",
                    "!",
                    "pekinfer",
                    f"opchain-path={descriptor}",
                    "active=true",
                    "!",
                    "filesink",
                    f"location={output}",
                ],
                env=environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )

            stdout = ""
            stderr = ""
            try:
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    frame_was_forwarded = output.exists() and output.stat().st_size > 0
                    if started.exists() and frame_was_forwarded:
                        break
                    if process.poll() is not None:
                        stdout, stderr = process.communicate()
                        self.fail(
                            "pipeline exited before setup and streaming overlapped:\n"
                            f"{stdout}\n{stderr}"
                        )
                    time.sleep(0.01)
                else:
                    self.fail(
                        "streaming did not progress while model setup was blocked"
                    )

                self.assertIsNone(
                    process.poll(),
                    "pipeline exited while model setup was still blocked",
                )
            finally:
                release.touch()
                try:
                    stdout, stderr = process.communicate(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    stdout, stderr = process.communicate()

            self.assertEqual(process.returncode, 0, f"{stdout}\n{stderr}")


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
