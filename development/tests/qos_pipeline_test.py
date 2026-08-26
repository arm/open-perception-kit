#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Verify opt-in QoS feedback, inference skipping, recovery, and state reset."""

from __future__ import annotations

import ctypes
import gc
import json
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest
from typing import Any

# Meson passes the test-only Delay Op first. OpChain loads that shared module through
# its plugin ABI; it is not a GStreamer plugin and must not be given to load_file().
DELAY_OP_PATH = Path(sys.argv[1]).resolve()
GST_PLUGIN_PATHS = [Path(argument).resolve() for argument in sys.argv[2:]]


class DelayOpController:
    """Control the test-only Delay Op without adding hooks to production code."""

    def __init__(self, path: Path) -> None:
        self._library = ctypes.CDLL(str(path))
        self._library.pek_test_qos_delay_create_handle.argtypes = []
        self._library.pek_test_qos_delay_create_handle.restype = ctypes.c_void_p
        self._library.pek_test_qos_delay_destroy_handle.argtypes = [ctypes.c_void_p]
        self._library.pek_test_qos_delay_set_milliseconds.argtypes = [
            ctypes.c_void_p,
            ctypes.c_uint64
        ]
        self._library.pek_test_qos_delay_set_milliseconds.restype = ctypes.c_bool
        self._library.pek_test_qos_delay_block_next_process.argtypes = [
            ctypes.c_void_p
        ]
        self._library.pek_test_qos_delay_block_next_process.restype = ctypes.c_bool
        self._library.pek_test_qos_delay_wait_until_blocked.argtypes = [
            ctypes.c_void_p,
            ctypes.c_uint64
        ]
        self._library.pek_test_qos_delay_wait_until_blocked.restype = ctypes.c_bool
        self._library.pek_test_qos_delay_release_process.argtypes = [
            ctypes.c_void_p
        ]
        self._library.pek_test_qos_delay_release_process.restype = ctypes.c_bool
        self.handle = self._library.pek_test_qos_delay_create_handle()
        if not self.handle:
            raise AssertionError("Delay Op control handle could not be created")

    def __enter__(self) -> DelayOpController:
        return self

    def __exit__(self, _type: object, _value: object, _traceback: object) -> None:
        self.close()

    def close(self) -> None:
        if self.handle:
            self._library.pek_test_qos_delay_destroy_handle(self.handle)
            self.handle = None

    def set_delay(self, milliseconds: int) -> None:
        if not self._library.pek_test_qos_delay_set_milliseconds(
            self.handle, milliseconds
        ):
            raise AssertionError("Delay Op instance is not available")

    def block_next_process(self) -> None:
        if not self._library.pek_test_qos_delay_block_next_process(
            self.handle
        ):
            raise AssertionError("Delay Op instance is not available")

    def wait_until_blocked(self, timeout_milliseconds: int = 1000) -> bool:
        return bool(
            self._library.pek_test_qos_delay_wait_until_blocked(
                self.handle, timeout_milliseconds
            )
        )

    def release_process(self) -> None:
        if not self._library.pek_test_qos_delay_release_process(
            self.handle
        ):
            raise AssertionError("Delay Op instance is not available")


class EventFlowMonitor:
    """Observe QoS events and buffers crossing the inference element."""

    def __init__(
        self, source_element: Any, infer_element: Any, gst: Any, gobject: Any
    ) -> None:
        self._source_element = source_element
        self._gst = gst
        self._gobject = gobject
        self._buffer_forwarded = threading.Event()
        self._qos_event_received = threading.Event()
        self.forwarded_qos_events = 0
        self.received_qos_events = 0
        self.infer_has_frame_results_meta = False
        self._probes = []

        # Events counted here escaped every pekinfer instance. An enabled, active
        # instance should consume QoS before it reaches this upstream source pad.
        source_pad = source_element.get_static_pad("src")
        self._probes.append(
            (
                source_pad,
                source_pad.add_probe(
                    gst.PadProbeType.EVENT_UPSTREAM,
                    self._observe_upstream_event,
                    None,
                ),
            )
        )
        # Observe pekinfer directly: downstream elements retain their default QoS
        # behavior and may legitimately drop the buffer later.
        infer_pad = infer_element.get_static_pad("src")
        self._probes.append(
            (
                infer_pad,
                infer_pad.add_probe(
                    gst.PadProbeType.BUFFER, self._observe_infer_buffer, None
                ),
            )
        )
        self._probes.append(
            (
                infer_pad,
                infer_pad.add_probe(
                    gst.PadProbeType.EVENT_UPSTREAM,
                    self._observe_infer_upstream_event,
                    None,
                ),
            )
        )

    def close(self) -> None:
        for pad, probe_id in self._probes:
            pad.remove_probe(probe_id)
        self._probes.clear()
        self._source_element = None

    def _observe_upstream_event(
        self, _pad: Any, info: Any, _data: Any
    ) -> Any:
        event = info.get_event()
        if event is not None and event.type == self._gst.EventType.QOS:
            self.forwarded_qos_events += 1
        return self._gst.PadProbeReturn.OK

    def _observe_infer_buffer(
        self, _pad: Any, info: Any, _data: Any
    ) -> Any:
        # FrameResultsMeta is registered when the pipeline starts processing, so its
        # GObject type cannot be resolved when this monitor is constructed.
        frame_results_meta_api = self._gobject.type_from_name(
            "com_arm_pek_meta_FrameResultsAPI_v1"
        )
        buffer = info.get_buffer()
        self.infer_has_frame_results_meta = bool(
            frame_results_meta_api
            and buffer is not None
            and buffer.get_meta(frame_results_meta_api) is not None
        )
        self._buffer_forwarded.set()
        return self._gst.PadProbeReturn.OK

    def _observe_infer_upstream_event(
        self, _pad: Any, info: Any, _data: Any
    ) -> Any:
        event = info.get_event()
        if event is not None and event.type == self._gst.EventType.QOS:
            self.received_qos_events += 1
            self._qos_event_received.set()
        return self._gst.PadProbeReturn.OK

    def wait_for_qos_event(self, timeout_seconds: float = 1.0) -> bool:
        return self._qos_event_received.wait(timeout_seconds)

    def push_buffer(self, pts: int) -> None:
        self._buffer_forwarded.clear()
        self.infer_has_frame_results_meta = False
        buffer = self._gst.Buffer.new_allocate(None, 16 * 16 * 4, None)
        buffer.pts = pts
        buffer.duration = self._gst.SECOND // 30
        if (
            self._source_element.emit("push-buffer", buffer)
            != self._gst.FlowReturn.OK
        ):
            raise AssertionError("appsrc did not accept the video frame")
        if not self._buffer_forwarded.wait(1):
            raise AssertionError("pekinfer did not forward the video frame")


class ContentDependencyPipelineTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        import gi

        gi.require_version("Gst", "1.0")
        from gi.repository import Gst

        Gst.init(None)
        for plugin_path in GST_PLUGIN_PATHS:
            Gst.Plugin.load_file(str(plugin_path))
        cls.Gst = Gst

    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory(prefix="pek-content-dependency-")
        self.addCleanup(self.directory.cleanup)

        capabilities = {
            "unrelated": {"provided-content-type": "genericObject"},
            "face-a": {"provided-content-type": "humanFace"},
            "face-b": {"provided-content-type": "humanFace"},
            "contact": {
                "required-content-type": "humanFace",
                "provided-content-type": "cameraContact",
            },
            "gaze": {"required-content-type": "cameraContact"},
        }
        descriptors = {}
        for name, attributes in capabilities.items():
            descriptor = Path(self.directory.name) / f"opchain-{name}.json"
            descriptor.write_text(
                json.dumps(
                    {
                        "version": 1,
                        "name": f"content-dependency-{name}",
                        "description": "Content dependency integration test OpChain",
                        "ops": [
                            {
                                "id": "pek-test-qos-delay/Delay",
                                "attributes": attributes,
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            descriptors[name] = descriptor

        chain = " ! ".join(
            f'pekinfer name={name} opchain-path="{descriptors[name]}" active=false'
            for name in capabilities
        )
        self.pipeline = self.Gst.parse_launch(
            "appsrc name=source is-live=true format=time "
            "caps=video/x-raw,format=BGRA,width=16,height=16 ! "
            f"{chain} ! fakesink async=false sync=false"
        )
        self.addCleanup(self.stop_pipeline)
        self.elements = {
            name: self.pipeline.get_by_name(name) for name in capabilities
        }

    def stop_pipeline(self) -> None:
        if self.pipeline is not None:
            self.pipeline.set_state(self.Gst.State.NULL)
            self.pipeline = None
            gc.collect()

    def test_enabling_model_activates_all_matching_upstream_dependencies(self) -> None:
        self.assertNotEqual(
            self.pipeline.set_state(self.Gst.State.PLAYING),
            self.Gst.StateChangeReturn.FAILURE,
        )
        self.pipeline.get_state(self.Gst.SECOND)

        self.elements["gaze"].set_property("active", True)

        for name in ("gaze", "contact", "face-a", "face-b"):
            self.assertTrue(self.elements[name].get_property("active"), name)
        self.assertFalse(self.elements["unrelated"].get_property("active"))


class QosPipelineTest(unittest.TestCase):
    PEKSINK_TESTS = {
        "test_qos_is_disabled_by_default",
        "test_enabling_peksink_uses_only_drain_native_qos",
        "test_peksink_feedback_reaches_pekinfer",
    }

    @classmethod
    def setUpClass(cls) -> None:
        import gi

        gi.require_version("Gst", "1.0")
        from gi.repository import GObject, Gst

        Gst.init(None)
        for plugin_path in GST_PLUGIN_PATHS:
            Gst.Plugin.load_file(str(plugin_path))
        cls.GObject = GObject
        cls.Gst = Gst

    def setUp(self) -> None:
        self.uses_peksink = self._testMethodName in self.PEKSINK_TESTS
        self.delay_op = DelayOpController(DELAY_OP_PATH)
        self.addCleanup(self.delay_op.close)
        self.directory = tempfile.TemporaryDirectory(prefix="pek-qos-")
        self.addCleanup(self.directory.cleanup)

        descriptors = {}
        for control_id in ("inactive", "infer"):
            descriptor = Path(self.directory.name) / f"opchain-{control_id}.json"
            descriptor.write_text(
                json.dumps(
                    {
                        "version": 1,
                        "name": f"qos-test-{control_id}",
                        "description": "QoS integration test opchain",
                        "ops": [
                            {
                                "id": "pek-test-qos-delay/Delay",
                                "attributes": (
                                    {"control-handle": self.delay_op.handle}
                                    if control_id == "infer"
                                    else {}
                                ),
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            descriptors[control_id] = descriptor

        output = (
            "peksink name=output"
            if self.uses_peksink
            else "fakesink name=output async=false sync=false qos=false"
        )
        self.pipeline = self.Gst.parse_launch(
            # The inactive instance verifies that QoS reaches the next active
            # inference element instead of being consumed unconditionally.
            "appsrc name=source is-live=true format=time "
            "caps=video/x-raw,format=BGRA,width=16,height=16 ! "
            f'pekinfer name=inactive opchain-path="{descriptors["inactive"]}" '
            "active=false ! "
            f'pekinfer name=infer opchain-path="{descriptors["infer"]}" '
            "active=true ! "
            "pektracker name=tracker max-missed-frames=15 qos=false ! "
            "pekperformance name=performance enabled=false qos=false ! "
            "pekosd name=osd enabled=false qos=false ! "
            f"{output}"
        )
        self.addCleanup(self.stop_pipeline)

        element_names = (
            "source",
            "inactive",
            "infer",
            "tracker",
            "performance",
            "osd",
            "output",
        )
        self.elements = {
            name: self.pipeline.get_by_name(name) for name in element_names
        }
        self.assertIsNotNone(self.elements["source"])
        if self.uses_peksink:
            self.elements["drain_fakesink"] = self.elements["output"].get_by_name(
                "drain_fakesink"
            )
            self.elements["vconv"] = self.elements["output"].get_by_name("vconv")
            self.assertIsNotNone(self.elements["drain_fakesink"])
        self.flow_monitor = EventFlowMonitor(
            self.elements["source"], self.elements["infer"], self.Gst, self.GObject
        )
        self.qos_accepted = threading.Event()
        self.elements["infer"].connect(
            "notify::qos-accepted-events-debug", self._on_qos_accepted
        )
        self.frame_duration = self.Gst.SECOND // 30

    def _on_qos_accepted(self, _element: Any, _property: Any) -> None:
        self.qos_accepted.set()

    def stop_pipeline(self) -> None:
        if self.pipeline is None:
            return
        self.pipeline.set_state(self.Gst.State.NULL)
        if hasattr(self, "flow_monitor"):
            self.flow_monitor.close()
            self.flow_monitor = None
        self.elements.clear()
        self.pipeline = None
        # Pad probes and PyGObject wrappers can form cycles. Collect them before the
        # next test creates another pipeline.
        gc.collect()

    def start_pipeline(self, delay_milliseconds: int = 0) -> None:
        self.assertNotEqual(
            self.pipeline.set_state(self.Gst.State.PLAYING),
            self.Gst.StateChangeReturn.FAILURE,
        )
        # get_state() waits for an asynchronous state transition; it does not change
        # the requested state itself.
        state_change, current_state, pending_state = self.pipeline.get_state(
            self.Gst.SECOND
        )
        self.assertIn(
            state_change,
            (
                self.Gst.StateChangeReturn.SUCCESS,
                self.Gst.StateChangeReturn.NO_PREROLL,
            ),
        )
        self.assertEqual(current_state, self.Gst.State.PLAYING)
        self.assertEqual(pending_state, self.Gst.State.VOID_PENDING)
        self.delay_op.set_delay(delay_milliseconds)

    def enable_inference_qos(self) -> None:
        self.elements["infer"].set_property("qos-enabled", True)

    def enable_controlled_inference_qos(self) -> None:
        self.enable_inference_qos()
        if self.uses_peksink:
            # These tests inject exact events. Native converter feedback would add
            # unrelated QoS messages to the same pipeline bus.
            self.elements["vconv"].set_property("qos", False)

    def establish_segment(self) -> None:
        # appsrc sends its initial SEGMENT with the first buffer, and pekinfer
        # correctly clears QoS state on that event. Prime it before exact events,
        # then reset counters so each test still begins from zero.
        self.delay_op.set_delay(0)
        self.flow_monitor.push_buffer(0)
        self.elements["infer"].set_property("qos-enabled", False)
        self.elements["infer"].set_property("qos-enabled", True)

    def push_qos_event(
        self, lateness: int, timestamp: int, proportion: float = 0.75
    ) -> None:
        infer_src_pad = self.elements["infer"].get_static_pad("src")
        self.assertTrue(
            infer_src_pad.send_event(
                self.Gst.Event.new_qos(
                    self.Gst.QOSType.UNDERFLOW,
                    proportion,
                    lateness,
                    timestamp,
                )
            ),
            "the controlled QoS event was not accepted",
        )

    def pop_qos_message(self, failure_message: str) -> Any:
        deadline = time.monotonic_ns() + self.Gst.SECOND
        while (remaining := deadline - time.monotonic_ns()) > 0:
            message = self.pipeline.get_bus().timed_pop_filtered(
                remaining,
                self.Gst.MessageType.QOS | self.Gst.MessageType.ERROR,
            )
            if message is None:
                break
            if message.type == self.Gst.MessageType.ERROR:
                error, debug = message.parse_error()
                self.fail(f"pipeline error: {error.message}: {debug}")
            if message.src.get_name() == "infer":
                return message
        self.fail(failure_message)

    def assert_no_qos_message(self, failure_message: str) -> None:
        self.assertIsNone(
            self.pipeline.get_bus().timed_pop_filtered(
                100 * self.Gst.MSECOND,
                self.Gst.MessageType.QOS | self.Gst.MessageType.ERROR,
            ),
            failure_message,
        )

    def test_qos_is_disabled_by_default(self) -> None:
        self.assertFalse(self.elements["infer"].get_property("qos-enabled"))
        self.assertFalse(self.elements["output"].get_property("qos-enabled"))
        self.assertFalse(self.elements["drain_fakesink"].get_property("sync"))
        self.assertFalse(self.elements["drain_fakesink"].get_property("qos"))
        self.assertTrue(self.elements["vconv"].get_property("qos"))

    def test_enabling_peksink_uses_only_drain_native_qos(self) -> None:
        self.elements["output"].set_property("qos-enabled", True)

        self.assertTrue(self.elements["drain_fakesink"].get_property("sync"))
        self.assertTrue(self.elements["drain_fakesink"].get_property("qos"))
        self.assertTrue(
            self.elements["vconv"].get_property("qos"),
            "peksink must preserve the converter's default QoS behavior",
        )
        for element_name in ("inactive", "infer", "tracker", "performance", "osd"):
            self.assertFalse(
                self.elements[element_name].get_property("qos"),
                f"{element_name} must not drop the main video buffer",
            )
        self.assertEqual(
            self.elements["tracker"].get_property("max-missed-frames"), 15
        )

    def test_peksink_feedback_reaches_pekinfer(self) -> None:
        self.enable_inference_qos()
        self.elements["output"].set_property("qos-enabled", True)
        self.start_pipeline()

        # Force the drain to treat buffers as late without a wall-clock sleep. The
        # first buffer establishes GstBaseSink's timing history; the second produces
        # native QoS feedback.
        self.elements["drain_fakesink"].set_property(
            "ts-offset", -self.Gst.SECOND
        )
        self.elements["drain_fakesink"].set_property("max-lateness", 0)
        feedback_start = 100 * self.Gst.MSECOND
        accepted_events = self.elements["infer"].get_property(
            "qos-accepted-events-debug"
        )
        self.qos_accepted.clear()
        self.flow_monitor.push_buffer(feedback_start)
        self.flow_monitor.push_buffer(feedback_start + self.frame_duration)
        self.assertTrue(
            self.flow_monitor.wait_for_qos_event(),
            "the enabled peksink drain did not generate upstream QoS",
        )
        self.assertGreaterEqual(self.flow_monitor.received_qos_events, 1)
        self.assertEqual(self.flow_monitor.forwarded_qos_events, 0)
        self.assertTrue(
            self.qos_accepted.wait(1),
            "pekinfer did not commit the native QoS event",
        )
        self.assertGreater(
            self.elements["infer"].get_property("qos-accepted-events-debug"),
            accepted_events,
        )

        self.flow_monitor.push_buffer(feedback_start + 2 * self.frame_duration)
        message = self.pop_qos_message(
            "real peksink feedback did not produce a pekinfer QoS message"
        )
        self.assertEqual(message.src.get_name(), "infer")

    def test_inactive_pekinfer_forwards_qos(self) -> None:
        self.enable_controlled_inference_qos()
        self.start_pipeline()

        infer_sink_pad = self.elements["infer"].get_static_pad("sink")
        self.assertEqual(self.flow_monitor.forwarded_qos_events, 0)
        self.assertTrue(
            infer_sink_pad.push_event(
                self.Gst.Event.new_qos(
                    self.Gst.QOSType.UNDERFLOW, 0.75, 1, 0
                )
            )
        )
        self.assertEqual(
            self.flow_monitor.forwarded_qos_events,
            1,
            "inactive pekinfer did not forward QoS",
        )

    def test_processing_latency_skips_only_the_next_stale_frame(self) -> None:
        self.enable_controlled_inference_qos()
        self.start_pipeline()
        self.establish_segment()
        self.delay_op.set_delay(50)
        proactive_start = 2 * self.Gst.SECOND
        self.push_qos_event(5 * self.Gst.MSECOND, proactive_start)
        self.assertEqual(
            self.flow_monitor.forwarded_qos_events,
            0,
            "active pekinfer forwarded QoS to an upstream video element",
        )

        # The 5 ms lateness has recovered by the next 30 FPS frame, so the 50 ms
        # Delay Op executes and makes only the immediately following frame stale.
        self.flow_monitor.push_buffer(proactive_start + self.frame_duration)
        self.assert_no_qos_message(
            "a recoverable timing spike incorrectly skipped inference"
        )
        self.flow_monitor.push_buffer(proactive_start + self.frame_duration + 1)
        message = self.pop_qos_message(
            "pekinfer did not proactively skip within its measured latency"
        )
        self.assertEqual(message.src.get_name(), "infer")
        values = message.parse_qos_values()
        self.assertEqual(values.jitter, 0)
        self.assertEqual(values.proportion, 1.0)

        self.flow_monitor.push_buffer(proactive_start + self.frame_duration + 2)
        self.assert_no_qos_message("proactive QoS skipped one frame too many")

    def test_lifecycle_reset_discards_in_flight_latency(self) -> None:
        self.enable_controlled_inference_qos()
        self.start_pipeline(delay_milliseconds=50)
        self.delay_op.block_next_process()
        push_errors = []

        def push_blocked_buffer() -> None:
            try:
                self.flow_monitor.push_buffer(3 * self.Gst.SECOND)
            except BaseException as error:  # Propagate worker assertions below.
                push_errors.append(error)

        push_thread = threading.Thread(target=push_blocked_buffer)
        push_thread.start()
        try:
            self.assertTrue(
                self.delay_op.wait_until_blocked(),
                "Delay Op did not enter its deterministic test gate",
            )
            self.elements["infer"].set_property("active", False)
            self.elements["infer"].set_property("active", True)
        finally:
            self.delay_op.release_process()
        push_thread.join(1)
        self.assertFalse(push_thread.is_alive(), "blocked frame did not finish")
        if push_errors:
            raise push_errors[0]

        self.delay_op.set_delay(0)
        self.flow_monitor.push_buffer(3 * self.Gst.SECOND + self.frame_duration)
        self.assert_no_qos_message(
            "in-flight inference restored QoS state after a lifecycle reset"
        )

    def test_event_skip_reports_standard_qos_fields_and_recovers(self) -> None:
        self.enable_controlled_inference_qos()
        self.start_pipeline()
        self.establish_segment()
        event_timestamp = 4 * self.Gst.SECOND
        self.push_qos_event(40 * self.Gst.MSECOND, event_timestamp)

        skipped_frame_pts = event_timestamp + self.frame_duration
        self.flow_monitor.push_buffer(skipped_frame_pts)
        self.assertTrue(
            self.flow_monitor.infer_has_frame_results_meta,
            "a QoS-skipped frame did not carry FrameResultsMeta",
        )
        message = self.pop_qos_message("pekinfer did not post a QoS message")
        self.assertEqual(message.src.get_name(), "infer")
        values = message.parse_qos_values()
        timing = message.parse_qos()
        stats = message.parse_qos_stats()
        self.assertAlmostEqual(values.proportion, 0.75)
        self.assertEqual(
            values.jitter,
            event_timestamp + 40 * self.Gst.MSECOND - skipped_frame_pts,
        )
        self.assertEqual(values.quality, self.Gst.FORMAT_PERCENT_MAX)
        self.assertEqual(timing.timestamp, skipped_frame_pts)
        self.assertEqual(stats.format, self.Gst.Format.BUFFERS)
        self.assertEqual(stats.processed, 0)
        self.assertEqual(stats.dropped, 1)

        self.flow_monitor.push_buffer(event_timestamp + 2 * self.frame_duration)
        self.assert_no_qos_message("inference did not resume after timestamps caught up")

    def test_recovery_boundary_is_inclusive(self) -> None:
        self.enable_controlled_inference_qos()
        self.start_pipeline()
        self.establish_segment()
        boundary_timestamp = 5 * self.Gst.SECOND
        self.push_qos_event(
            self.frame_duration, boundary_timestamp, proportion=1.0
        )

        self.flow_monitor.push_buffer(boundary_timestamp + self.frame_duration)
        message = self.pop_qos_message(
            "pekinfer processed a frame at the inclusive recovery boundary"
        )
        self.assertEqual(message.src.get_name(), "infer")

    def test_disabling_qos_clears_pending_state(self) -> None:
        self.enable_controlled_inference_qos()
        self.start_pipeline()
        self.establish_segment()
        self.push_qos_event(self.Gst.SECOND, 6 * self.Gst.SECOND)

        self.elements["infer"].set_property("qos-enabled", False)
        self.elements["infer"].set_property("qos-enabled", True)
        self.flow_monitor.push_buffer(6 * self.Gst.SECOND + self.frame_duration)
        self.assert_no_qos_message("QoS state survived disable and re-enable")

    def test_disabling_inference_clears_pending_qos_event(self) -> None:
        self.enable_controlled_inference_qos()
        self.start_pipeline()
        self.establish_segment()
        self.push_qos_event(self.Gst.SECOND, 7 * self.Gst.SECOND)

        self.elements["infer"].set_property("active", False)
        self.elements["infer"].set_property("active", True)
        self.flow_monitor.push_buffer(7 * self.Gst.SECOND + self.frame_duration)
        self.assert_no_qos_message("QoS event survived inference disable and re-enable")


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
