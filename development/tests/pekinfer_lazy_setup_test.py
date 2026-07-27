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
from collections.abc import Callable
from typing import Any

GST_LAUNCH = Path(sys.argv[1]).resolve()
PLUGIN_PATH = Path(sys.argv[2]).resolve()
BLOCKING_SETUP_OP_PATH = Path(sys.argv[3]).resolve()
FAKE_MODELFETCH_PATH = Path(sys.argv[4]).resolve()
DEFAULT_VIDEO_CAPS = "video/x-raw,format=BGRA,width=16,height=16"


def gst_launch_command(
    descriptor: Path,
    *,
    num_buffers: int,
    active: bool,
    live: bool = False,
    caps: str = DEFAULT_VIDEO_CAPS,
    sink: tuple[str, ...] = ("fakesink",),
) -> list[str]:
    command = [
        str(GST_LAUNCH),
        "-q",
        "videotestsrc",
        f"num-buffers={num_buffers}",
    ]
    if live:
        command.append("is-live=true")
    command.extend(
        [
            "!",
            caps,
            "!",
            "pekinfer",
            f"opchain-path={descriptor}",
            f"active={str(active).lower()}",
            "!",
            *sink,
        ]
    )
    return command


def download_calls(calls: Path) -> list[str]:
    if not calls.exists():
        return []
    return calls.read_text(encoding="utf-8").splitlines()


def wait_until(
    bus: Any,
    gst: Any,
    predicate: Callable[[], bool],
    description: str,
) -> None:
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if predicate():
            return
        message = bus.timed_pop_filtered(
            10 * gst.MSECOND,
            gst.MessageType.ERROR,
        )
        if message is not None:
            error, debug = message.parse_error()
            raise RuntimeError(f"{error.message}: {debug}")
    raise TimeoutError(f"timed out waiting for {description}")


def wait_for_setup_failure(bus: Any, gst: Any) -> None:
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        message = bus.timed_pop_filtered(
            50 * gst.MSECOND,
            gst.MessageType.ERROR | gst.MessageType.WARNING,
        )
        if message is None:
            continue
        if message.type == gst.MessageType.ERROR:
            error, debug = message.parse_error()
            raise RuntimeError(f"{error.message}: {debug}")
        warning, debug = message.parse_warning()
        if "Asynchronous OpChain setup failed" in warning.message:
            return
        raise RuntimeError(f"unexpected pipeline warning: {warning.message}: {debug}")
    raise TimeoutError("timed out waiting for asynchronous setup failure")


def create_activation_cycle_pipeline(gst: Any, descriptor: Path) -> tuple[Any, Any]:
    pipeline = gst.Pipeline.new("activation-cycle")
    source = gst.ElementFactory.make("videotestsrc", "source")
    caps_filter = gst.ElementFactory.make("capsfilter", "caps")
    infer = gst.ElementFactory.make("pekinfer", "infer")
    sink = gst.ElementFactory.make("fakesink", "sink")
    elements = [source, caps_filter, infer, sink]
    if pipeline is None or any(element is None for element in elements):
        raise RuntimeError("could not create the activation-cycle test pipeline")

    source.set_property("is-live", True)
    caps_filter.set_property(
        "caps",
        gst.Caps.from_string(DEFAULT_VIDEO_CAPS),
    )
    infer.set_property("opchain-path", str(descriptor))
    infer.set_property("active", True)
    sink.set_property("sync", False)

    for element in elements:
        pipeline.add(element)
    for current, following in zip(elements, elements[1:]):
        if not current.link(following):
            raise RuntimeError(
                f"could not link {current.get_name()} to {following.get_name()}"
            )

    return pipeline, infer


def run_activation_cycle_helper(scenario: str) -> None:
    import gi

    gi.require_version("Gst", "1.0")
    from gi.repository import Gst

    descriptor = Path(os.environ["PEK_TEST_ACTIVATION_DESCRIPTOR"])
    calls = Path(os.environ["PEK_MODELFETCH_FAKE_CALLS"])
    setup_started = Path(os.environ["PEK_TEST_BLOCKING_SETUP_STARTED"])
    setup_release = Path(os.environ["PEK_TEST_BLOCKING_SETUP_RELEASE"])
    process_called = Path(os.environ["PEK_TEST_BLOCKING_PROCESS_CALLED"])

    Gst.init(None)
    pipeline, infer = create_activation_cycle_pipeline(Gst, descriptor)
    bus = pipeline.get_bus()

    if pipeline.set_state(Gst.State.PLAYING) == Gst.StateChangeReturn.FAILURE:
        raise RuntimeError("could not start the activation-cycle test pipeline")

    try:
        if scenario == "retry":
            wait_for_setup_failure(bus, Gst)
            os.environ.pop("PEK_MODELFETCH_FAKE_MODE", None)
            infer.set_property("active", False)
            infer.set_property("active", True)
            wait_until(
                bus,
                Gst,
                process_called.exists,
                "the retried OpChain to process a frame",
            )
            if download_calls(calls) != ["call", "call"]:
                raise AssertionError(
                    "expected one failed and one successful materialization, got "
                    f"{download_calls(calls)}"
                )
        elif scenario == "cache-ready":
            wait_until(
                bus,
                Gst,
                lambda: setup_started.exists() and download_calls(calls) == ["call"],
                "setup to block after its first materialization",
            )
            infer.set_property("active", False)
            setup_release.touch()
            infer.set_property("active", True)
            wait_until(
                bus,
                Gst,
                process_called.exists,
                "the cached OpChain to process a frame",
            )
            if download_calls(calls) != ["call"]:
                raise AssertionError(
                    f"reactivation rematerialized the ready model: {download_calls(calls)}"
                )
        else:
            raise ValueError(f"unknown activation-cycle scenario: {scenario}")
    finally:
        pipeline.set_state(Gst.State.NULL)


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

    def write_fake_stored_model(self, test_directory: Path) -> str:
        relative_model_path = (
            Path("pekinfer-tests") / test_directory.name / "model.onnx"
        )
        stored_model = Path("/work/var/models") / relative_model_path
        stored_model.parent.mkdir(parents=True, exist_ok=True)
        stored_model.write_text("model", encoding="utf-8")
        self.addCleanup(shutil.rmtree, stored_model.parent, True)
        return (
            "hf:Arm/example@0123456789abcdef0123456789abcdef01234567"
            f"#file={relative_model_path.as_posix()}"
        )

    def run_activation_cycle(self, test_directory: Path, scenario: str) -> None:
        process_called = test_directory / "op-process-called"
        environment = self.pipeline_environment(test_directory)
        environment["PEK_TEST_ACTIVATION_DESCRIPTOR"] = str(
            self.write_model_loading_opchain(
                test_directory,
                f"activation-{scenario}",
                self.write_fake_stored_model(test_directory),
            )
        )
        environment["PEK_MODELFETCH_FAKE_CALLS"] = str(
            test_directory / "modelfetch-calls"
        )
        environment["PEK_TEST_BLOCKING_SETUP_STARTED"] = str(
            test_directory / "op-setup-started"
        )
        environment["PEK_TEST_BLOCKING_SETUP_RELEASE"] = str(
            test_directory / "op-setup-release"
        )
        environment["PEK_TEST_BLOCKING_PROCESS_CALLED"] = str(process_called)
        if scenario == "retry":
            environment["PEK_MODELFETCH_FAKE_MODE"] = "api-failure"
        if scenario == "retry":
            Path(environment["PEK_TEST_BLOCKING_SETUP_RELEASE"]).touch()

        result = subprocess.run(
            [
                sys.executable,
                str(Path(__file__).resolve()),
                *sys.argv[1:5],
                "--activation-cycle-helper",
                scenario,
            ],
            check=False,
            capture_output=True,
            env=environment,
            text=True,
            timeout=10,
        )

        self.assertEqual(result.returncode, 0, f"{result.stdout}\n{result.stderr}")
        self.assertTrue(process_called.exists(), "the ready OpChain was not executed")

    def run_active_setup_failure_pipeline(self) -> subprocess.CompletedProcess[str]:
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
                gst_launch_command(
                    descriptor,
                    num_buffers=15,
                    active=True,
                    live=True,
                ),
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
                gst_launch_command(
                    opchain_descriptor,
                    num_buffers=1,
                    active=False,
                ),
                check=False,
                capture_output=True,
                env=environment,
                text=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(calls.exists(), "inactive model triggered materialization")

    def test_active_setup_failure_keeps_pipeline_running(self) -> None:
        result = self.run_active_setup_failure_pipeline()
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
                gst_launch_command(
                    descriptor,
                    num_buffers=15,
                    active=True,
                    live=True,
                    sink=("filesink", f"location={output}"),
                ),
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
                gst_launch_command(
                    opchain_descriptor,
                    num_buffers=15,
                    active=True,
                    live=True,
                ),
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
            opchain_descriptor = self.write_model_loading_opchain(
                test_directory,
                "single-model-load",
                self.write_fake_stored_model(test_directory),
            )
            op_setup_started = test_directory / "op-setup-started"
            op_setup_release = test_directory / "op-setup-release"
            op_setup_release.touch()
            calls = test_directory / "modelfetch-calls"
            environment = self.pipeline_environment(test_directory)
            environment["PEK_MODELFETCH_FAKE_CALLS"] = str(calls)
            environment["PEK_TEST_BLOCKING_SETUP_STARTED"] = str(op_setup_started)
            environment["PEK_TEST_BLOCKING_SETUP_RELEASE"] = str(op_setup_release)

            result = subprocess.run(
                gst_launch_command(
                    opchain_descriptor,
                    num_buffers=180,
                    active=True,
                    live=True,
                    caps=f"{DEFAULT_VIDEO_CAPS},framerate=60/1",
                ),
                check=False,
                capture_output=True,
                env=environment,
                text=True,
                timeout=5,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(op_setup_started.exists(), "operation setup did not complete")
            self.assertEqual(calls.read_text(encoding="utf-8").splitlines(), ["call"])

    def test_failed_setup_retries_after_deactivation_cycle(self) -> None:
        with tempfile.TemporaryDirectory(prefix="pekinfer-retry-setup-") as directory:
            self.run_activation_cycle(Path(directory), "retry")

    def test_setup_completed_while_inactive_is_reused_on_reactivation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="pekinfer-cache-ready-") as directory:
            self.run_activation_cycle(Path(directory), "cache-ready")


if __name__ == "__main__":
    if len(sys.argv) == 7 and sys.argv[5] == "--activation-cycle-helper":
        run_activation_cycle_helper(sys.argv[6])
    else:
        unittest.main(argv=[sys.argv[0]])
