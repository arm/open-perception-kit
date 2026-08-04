#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Verify opt-in QoS feedback, inference skipping, recovery, and state reset."""

from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from typing import Any

# Meson passes the test-only Delay Op first. OpChain loads that shared module through
# its plugin ABI; it is not a GStreamer plugin and must not be given to load_file().
GST_PLUGIN_PATHS = [Path(argument).resolve() for argument in sys.argv[2:]]


class EventFlowMonitor:
    """Observe QoS events and buffers crossing the inference element."""

    def __init__(
        self, source_element: Any, infer_element: Any, gst: Any, gobject: Any
    ) -> None:
        self._source_element = source_element
        self._gst = gst
        self._gobject = gobject
        self._buffer_forwarded = threading.Event()
        self.forwarded_qos_events = 0
        self.infer_has_perception_meta = False

        # Events counted here escaped every pekinfer instance. An enabled, active
        # instance should consume QoS before it reaches this upstream source pad.
        source_element.get_static_pad("src").add_probe(
            gst.PadProbeType.EVENT_UPSTREAM, self._observe_upstream_event, None
        )
        # Observe pekinfer directly: downstream elements retain their default QoS
        # behavior and may legitimately drop the buffer later.
        infer_element.get_static_pad("src").add_probe(
            gst.PadProbeType.BUFFER, self._observe_infer_buffer, None
        )

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
        # PerceptionMeta is registered when the pipeline starts processing, so its
        # GObject type cannot be resolved when this monitor is constructed.
        perception_meta_api = self._gobject.type_from_name(
            "com_arm_pek_meta_PerceptionAPI_v1"
        )
        buffer = info.get_buffer()
        self.infer_has_perception_meta = bool(
            perception_meta_api
            and buffer is not None
            and buffer.get_meta(perception_meta_api) is not None
        )
        self._buffer_forwarded.set()
        return self._gst.PadProbeReturn.OK

    def push_buffer(self, pts: int) -> None:
        self._buffer_forwarded.clear()
        self.infer_has_perception_meta = False
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


class QosPipelineTest(unittest.TestCase):
    def test_qos_recovery_skips_only_inference_that_cannot_catch_up(self) -> None:
        import gi

        gi.require_version("Gst", "1.0")
        from gi.repository import GObject, Gst

        Gst.init(None)
        # Load only the GStreamer elements used by the pipeline. This avoids relying
        # on a machine-wide plugin installation during the test.
        for plugin_path in GST_PLUGIN_PATHS:
            Gst.Plugin.load_file(str(plugin_path))

        with tempfile.TemporaryDirectory(prefix="pek-qos-") as directory:
            # Both pekinfer instances use a deterministic 50 ms Op instead of a model.
            # At 30 FPS this exceeds one frame budget and schedules one proactive skip.
            descriptor = Path(directory) / "opchain.json"
            descriptor.write_text(
                json.dumps(
                    {
                        "name": "qos-test",
                        "ops": [
                            {
                                "id": "pek-test-qos-delay/Delay",
                                "attributes": {},
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            pipeline = Gst.parse_launch(
                # The inactive instance verifies that QoS reaches the next active
                # inference element instead of being consumed unconditionally.
                "appsrc name=source is-live=true format=time "
                "caps=video/x-raw,format=BGRA,width=16,height=16 ! "
                f'pekinfer name=inactive opchain-path="{descriptor}" active=false ! '
                f'pekinfer name=infer opchain-path="{descriptor}" active=true ! '
                "pektracker name=tracker max-missed-frames=15 qos=false ! "
                "pekperformance name=performance enabled=false qos=false ! "
                "pekosd name=osd enabled=false qos=false ! "
                "peksink name=output"
            )

            element_names = (
                "source",
                "inactive",
                "infer",
                "tracker",
                "performance",
                "osd",
                "output",
            )
            elements = {name: pipeline.get_by_name(name) for name in element_names}
            elements["drain_fakesink"] = elements["output"].get_by_name(
                "drain_fakesink"
            )
            elements["vconv"] = elements["output"].get_by_name("vconv")
            self.assertIsNotNone(elements["source"])
            self.assertIsNotNone(elements["drain_fakesink"])

            # The experimental path must preserve origin/develop behavior unless
            # explicitly enabled on both the inference policy and feedback source.
            self.assertFalse(elements["infer"].get_property("qos-enabled"))
            self.assertFalse(elements["output"].get_property("qos-enabled"))
            self.assertFalse(elements["drain_fakesink"].get_property("sync"))
            self.assertFalse(elements["drain_fakesink"].get_property("qos"))
            self.assertTrue(elements["vconv"].get_property("qos"))

            elements["infer"].set_property("qos-enabled", True)
            elements["output"].set_property("qos-enabled", True)

            # Enabling peksink only activates its drain's native clocked QoS. It must
            # not reconfigure the converter or other downstream processing elements.
            self.assertTrue(elements["drain_fakesink"].get_property("sync"))
            self.assertTrue(elements["drain_fakesink"].get_property("qos"))
            self.assertTrue(
                elements["vconv"].get_property("qos"),
                "peksink must preserve the converter's default QoS behavior",
            )
            for element_name in ("inactive", "infer", "tracker", "performance", "osd"):
                self.assertFalse(
                    elements[element_name].get_property("qos"),
                    f"{element_name} must not drop the main video buffer",
                )
            self.assertEqual(
                elements["tracker"].get_property("max-missed-frames"),
                15,
            )

            # The rest of this test injects exact QoS values, so disable automatic
            # feedback after verifying peksink's production configuration. Otherwise
            # converter messages and drops would make the policy assertions nondeterministic.
            elements["drain_fakesink"].set_property("sync", False)
            elements["drain_fakesink"].set_property("qos", False)
            elements["vconv"].set_property("qos", False)

            flow_monitor = EventFlowMonitor(
                elements["source"], elements["infer"], Gst, GObject
            )

            self.assertNotEqual(
                pipeline.set_state(Gst.State.PLAYING),
                Gst.StateChangeReturn.FAILURE,
            )
            try:
                # set_state() may complete asynchronously. get_state() does not
                # change state, but blocks for up to one second for that transition.
                state_change, current_state, pending_state = pipeline.get_state(
                    Gst.SECOND
                )
                self.assertIn(
                    state_change,
                    (Gst.StateChangeReturn.SUCCESS, Gst.StateChangeReturn.NO_PREROLL),
                )
                self.assertEqual(current_state, Gst.State.PLAYING)
                self.assertEqual(pending_state, Gst.State.VOID_PENDING)

                # Inject QoS upstream of the active infer. The inactive infer must
                # forward it to the source probe rather than consume it.
                infer_sink_pad = elements["infer"].get_static_pad("sink")
                self.assertEqual(flow_monitor.forwarded_qos_events, 0)
                self.assertTrue(
                    infer_sink_pad.push_event(
                        Gst.Event.new_qos(Gst.QOSType.UNDERFLOW, 0.75, 1, 0)
                    )
                )
                self.assertEqual(
                    flow_monitor.forwarded_qos_events,
                    1,
                    "inactive pekinfer did not forward QoS",
                )

                drain_pad = elements["drain_fakesink"].get_static_pad("sink")
                sent = drain_pad.push_event(
                    Gst.Event.new_qos(
                        Gst.QOSType.UNDERFLOW,
                        0.75,
                        5 * Gst.MSECOND,
                        0,
                    )
                )
                self.assertTrue(sent, "the downstream QoS event was not accepted")
                self.assertEqual(
                    flow_monitor.forwarded_qos_events,
                    1,
                    "active pekinfer forwarded QoS to an upstream video element",
                )

                # The 5 ms lateness has already recovered by the next 30 FPS frame,
                # so this frame executes the 50 ms Delay Op rather than being skipped.
                frame_duration = Gst.SECOND // 30
                flow_monitor.push_buffer(frame_duration)
                self.assertIsNone(
                    pipeline.get_bus().timed_pop_filtered(
                        100 * Gst.MSECOND,
                        Gst.MessageType.QOS | Gst.MessageType.ERROR,
                    ),
                    "a recoverable timing spike incorrectly skipped inference",
                )

                # That 50 ms execution crossed one later frame deadline. The next
                # buffer is forwarded without running the Delay Op and reports QoS.
                flow_monitor.push_buffer(frame_duration + 1)
                proactive_message = pipeline.get_bus().timed_pop_filtered(
                    Gst.SECOND, Gst.MessageType.QOS | Gst.MessageType.ERROR
                )
                self.assertIsNotNone(
                    proactive_message,
                    "pekinfer did not proactively skip within its measured latency",
                )
                if proactive_message.type == Gst.MessageType.ERROR:
                    error, debug = proactive_message.parse_error()
                    self.fail(f"pipeline error: {error.message}: {debug}")
                self.assertEqual(proactive_message.src.get_name(), "infer")
                proactive_values = proactive_message.parse_qos_values()
                self.assertEqual(proactive_values.jitter, 0)
                self.assertEqual(proactive_values.proportion, 1.0)

                # Only one following frame was stale; the subsequent buffer executes.
                flow_monitor.push_buffer(frame_duration + 2)
                self.assertIsNone(
                    pipeline.get_bus().timed_pop_filtered(
                        100 * Gst.MSECOND,
                        Gst.MessageType.QOS | Gst.MessageType.ERROR,
                    ),
                    "proactive QoS skipped one frame too many",
                )

                event_timestamp = frame_duration
                # Recovery point is timestamp + lateness (~73 ms). A frame at ~66 ms
                # must skip inference, while the following ~99 ms frame must resume.
                sent = drain_pad.push_event(
                    Gst.Event.new_qos(
                        Gst.QOSType.UNDERFLOW,
                        0.75,
                        40 * Gst.MSECOND,
                        event_timestamp,
                    )
                )
                self.assertTrue(sent, "the second QoS event was not accepted")

                skipped_frame_pts = event_timestamp + frame_duration
                flow_monitor.push_buffer(skipped_frame_pts)
                self.assertTrue(
                    flow_monitor.infer_has_perception_meta,
                    "a QoS-skipped frame did not carry PerceptionMeta",
                )
                message = pipeline.get_bus().timed_pop_filtered(
                    Gst.SECOND, Gst.MessageType.QOS | Gst.MessageType.ERROR
                )
                self.assertIsNotNone(message, "pekinfer did not post a QoS message")
                if message.type == Gst.MessageType.ERROR:
                    error, debug = message.parse_error()
                    self.fail(f"pipeline error: {error.message}: {debug}")

                self.assertEqual(message.src.get_name(), "infer")
                values = message.parse_qos_values()
                timing = message.parse_qos()
                self.assertAlmostEqual(values.proportion, 0.75)
                self.assertEqual(values.jitter, 40 * Gst.MSECOND)
                self.assertEqual(timing.timestamp, skipped_frame_pts)

                flow_monitor.push_buffer(event_timestamp + 2 * frame_duration)
                unexpected = pipeline.get_bus().timed_pop_filtered(
                    100 * Gst.MSECOND,
                    Gst.MessageType.QOS | Gst.MessageType.ERROR,
                )
                self.assertIsNone(
                    unexpected,
                    "inference did not resume after timestamps caught up; "
                    f"message source={unexpected.src.get_name() if unexpected else 'none'}",
                )

                self.assertTrue(
                    drain_pad.push_event(
                        Gst.Event.new_qos(
                            Gst.QOSType.UNDERFLOW,
                            0.75,
                            Gst.SECOND,
                            0,
                        )
                    )
                )
                # Disabling the policy clears pending QoS and proactive-skip state.
                elements["infer"].set_property("qos-enabled", False)
                elements["infer"].set_property("qos-enabled", True)
                flow_monitor.push_buffer(event_timestamp + 3 * frame_duration)
                self.assertIsNone(
                    pipeline.get_bus().timed_pop_filtered(
                        100 * Gst.MSECOND,
                        Gst.MessageType.QOS | Gst.MessageType.ERROR,
                    ),
                    "QoS state survived disable and re-enable",
                )
            finally:
                pipeline.set_state(Gst.State.NULL)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
