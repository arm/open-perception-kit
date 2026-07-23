#!/usr/bin/env python3
################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

"""Exercise pekinfer's activation boundary through the built GStreamer plugin."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

GST_LAUNCH = Path(sys.argv[1]).resolve()
PLUGIN_PATH = Path(sys.argv[2]).resolve()
BLOCKING_SETUP_OP_PATH = Path(sys.argv[3]).resolve()
FAKE_MODELFETCH_PATH = Path(sys.argv[4]).resolve()


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
        existing_preload = environment.get("LD_PRELOAD")
        environment["LD_PRELOAD"] = str(FAKE_MODELFETCH_PATH)
        if existing_preload:
            environment["LD_PRELOAD"] += os.pathsep + existing_preload
        return environment

    @staticmethod
    def write_model_loading_opchain(
        test_directory: Path,
        name: str,
        model_file: str = (
            "hf:Arm/example@0123456789abcdef0123456789abcdef01234567"
            "#file=model.onnx"
        ),
    ) -> Path:
        model_descriptor = test_directory / "model.json"
        model_descriptor.write_text(
            json.dumps(
                {
                    "name": name,
                    "modelFile": model_file,
                    "modelFamily": "test",
                    "dynamicOutput": True,
                }
            ),
            encoding="utf-8",
        )
        opchain_descriptor = test_directory / "opchain.json"
        opchain_descriptor.write_text(
            json.dumps(
                {
                    "name": name,
                    "ops": [
                        {
                            "id": "pek-test-blocking-setup/BlockingSetup",
                            "attributes": {
                                "modelDescriptor": str(model_descriptor),
                            },
                        }
                    ],
                }
            ),
            encoding="utf-8",
        )
        return opchain_descriptor

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

    def test_inactive_model_does_not_materialize_descriptor(self) -> None:
        with tempfile.TemporaryDirectory(prefix="pekinfer-inactive-model-") as directory:
            test_directory = Path(directory)
            opchain_descriptor = self.write_model_loading_opchain(
                test_directory,
                "inactive-model",
            )
            calls = test_directory / "modelfetch-calls"
            environment = self.pipeline_environment(test_directory)
            environment["PEK_MODELFETCH_FAKE_MODE"] = "downloaded"
            environment["PEK_MODELFETCH_FAKE_CALLS"] = str(calls)

            result = subprocess.run(
                [
                    str(GST_LAUNCH),
                    "-q",
                    "videotestsrc",
                    "num-buffers=1",
                    "!",
                    "video/x-raw,format=BGRA,width=16,height=16",
                    "!",
                    "pekinfer",
                    f"opchain-path={opchain_descriptor}",
                    "active=false",
                    "!",
                    "fakesink",
                ],
                check=False,
                capture_output=True,
                env=environment,
                text=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(calls.exists(), "inactive model triggered materialization")

    def test_active_setup_failure_keeps_pipeline_running(self) -> None:
        result = self.run_pipeline(active=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Asynchronous OpChain setup failed", result.stderr)

    def test_blocking_model_materialization_does_not_block_streaming_thread(self) -> None:
        with tempfile.TemporaryDirectory(prefix="pekinfer-async-setup-") as directory:
            test_directory = Path(directory)
            descriptor = self.write_model_loading_opchain(
                test_directory,
                "blocking-model-load",
            )
            materialization_entered = test_directory / "materialization-entered"
            op_setup_started = test_directory / "op-setup-started"
            release = test_directory / "setup-release"
            output = test_directory / "frame.raw"
            environment = self.pipeline_environment(test_directory)
            environment["PEK_MODELFETCH_FAKE_MODE"] = "blocking"
            environment["PEK_MODELFETCH_FAKE_ENTERED"] = str(materialization_entered)
            environment["PEK_TEST_BLOCKING_SETUP_STARTED"] = str(op_setup_started)
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
                    if materialization_entered.exists() and frame_was_forwarded:
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

                self.assertFalse(
                    op_setup_started.exists(),
                    "the operation was configured before its model became available",
                )
            finally:
                release.touch()
                try:
                    stdout, stderr = process.communicate(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    stdout, stderr = process.communicate()

            self.assertEqual(process.returncode, 0, f"{stdout}\n{stderr}")

    def test_pipeline_teardown_cancels_model_materialization(self) -> None:
        with tempfile.TemporaryDirectory(prefix="pekinfer-cancel-model-load-") as directory:
            test_directory = Path(directory)
            opchain_descriptor = self.write_model_loading_opchain(
                test_directory,
                "cancel-model-load",
            )
            materialization_entered = test_directory / "materialization-entered"
            op_setup_started = test_directory / "op-setup-started"
            op_setup_release = test_directory / "op-setup-release"
            environment = self.pipeline_environment(test_directory)
            environment["PEK_MODELFETCH_FAKE_MODE"] = "blocking"
            environment["PEK_MODELFETCH_FAKE_ENTERED"] = str(materialization_entered)
            environment["PEK_TEST_BLOCKING_SETUP_STARTED"] = str(op_setup_started)
            environment["PEK_TEST_BLOCKING_SETUP_RELEASE"] = str(op_setup_release)

            result = subprocess.run(
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
                    f"opchain-path={opchain_descriptor}",
                    "active=true",
                    "!",
                    "fakesink",
                ],
                check=False,
                capture_output=True,
                env=environment,
                text=True,
                timeout=5,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(
                materialization_entered.exists(),
                "model materialization was not entered",
            )
            self.assertFalse(
                op_setup_started.exists(),
                "the operation was configured before its model became available",
            )

    def test_successful_model_setup_materializes_descriptor_once(self) -> None:
        with tempfile.TemporaryDirectory(prefix="pekinfer-single-model-load-") as directory:
            test_directory = Path(directory)
            relative_model_path = (
                Path("pekinfer-tests") / test_directory.name / "model.onnx"
            )
            stored_model = Path("/work/var/models") / relative_model_path
            stored_model.parent.mkdir(parents=True, exist_ok=True)
            stored_model.write_text("model", encoding="utf-8")
            self.addCleanup(shutil.rmtree, stored_model.parent, True)

            opchain_descriptor = self.write_model_loading_opchain(
                test_directory,
                "single-model-load",
                (
                    "hf:Arm/example@0123456789abcdef0123456789abcdef01234567"
                    f"#file={relative_model_path.as_posix()}"
                ),
            )
            op_setup_started = test_directory / "op-setup-started"
            op_setup_release = test_directory / "op-setup-release"
            op_setup_release.touch()
            calls = test_directory / "modelfetch-calls"
            environment = self.pipeline_environment(test_directory)
            environment["PEK_MODELFETCH_FAKE_MODE"] = "existing"
            environment["PEK_MODELFETCH_FAKE_CALLS"] = str(calls)
            environment["PEK_TEST_BLOCKING_SETUP_STARTED"] = str(op_setup_started)
            environment["PEK_TEST_BLOCKING_SETUP_RELEASE"] = str(op_setup_release)

            result = subprocess.run(
                [
                    str(GST_LAUNCH),
                    "-q",
                    "videotestsrc",
                    "num-buffers=180",
                    "is-live=true",
                    "!",
                    "video/x-raw,format=BGRA,width=16,height=16,framerate=60/1",
                    "!",
                    "pekinfer",
                    f"opchain-path={opchain_descriptor}",
                    "active=true",
                    "!",
                    "fakesink",
                ],
                check=False,
                capture_output=True,
                env=environment,
                text=True,
                timeout=5,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(op_setup_started.exists(), "operation setup did not complete")
            self.assertEqual(calls.read_text(encoding="utf-8").splitlines(), ["call"])


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
