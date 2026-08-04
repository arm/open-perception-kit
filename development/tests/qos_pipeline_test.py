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

            source = pipeline.get_by_name("source")
            infer = pipeline.get_by_name("infer")
            output = pipeline.get_by_name("output")
            drain = output.get_by_name("drain_fakesink")
            video_converter = output.get_by_name("vconv")
            self.assertIsNotNone(source)
            self.assertIsNotNone(drain)

            # The experimental path must preserve origin/develop behavior unless
            # explicitly enabled on both the inference policy and feedback source.
            self.assertFalse(infer.get_property("qos-enabled"))
            self.assertFalse(output.get_property("qos-enabled"))
            self.assertFalse(drain.get_property("sync"))
            self.assertFalse(drain.get_property("qos"))
            self.assertTrue(video_converter.get_property("qos"))

            infer.set_property("qos-enabled", True)
            output.set_property("qos-enabled", True)

            # Enabling peksink only activates its drain's native clocked QoS. It must
            # not reconfigure the converter or other downstream processing elements.
            self.assertTrue(drain.get_property("sync"))
            self.assertTrue(drain.get_property("qos"))
            self.assertTrue(
                video_converter.get_property("qos"),
                "peksink must preserve the converter's default QoS behavior",
            )
            for element_name in ("inactive", "infer", "tracker", "performance", "osd"):
                self.assertFalse(
                    pipeline.get_by_name(element_name).get_property("qos"),
                    f"{element_name} must not drop the main video buffer",
                )
            self.assertEqual(
                pipeline.get_by_name("tracker").get_property("max-missed-frames"),
                15,
            )

            # The rest of this test injects exact QoS values, so disable automatic
            # feedback after verifying peksink's production configuration. Otherwise
            # converter messages and drops would make the policy assertions nondeterministic.
            drain.set_property("sync", False)
            drain.set_property("qos", False)
            video_converter.set_property("qos", False)

            forwarded = 0
            buffer_forwarded = threading.Event()
            infer_has_perception_meta = False

            def observe_upstream_event(
                _pad: Any, info: Any, _data: Any
            ) -> Gst.PadProbeReturn:
                nonlocal forwarded
                event = info.get_event()
                if event is not None and event.type == Gst.EventType.QOS:
                    forwarded += 1
                return Gst.PadProbeReturn.OK

            source_pad = source.get_static_pad("src")
            # Events counted here escaped every pekinfer instance. An enabled, active
            # instance should consume QoS before it reaches this upstream source pad.
            source_pad.add_probe(
                Gst.PadProbeType.EVENT_UPSTREAM, observe_upstream_event, None
            )

            def observe_infer_buffer(
                _pad: Any, info: Any, _data: Any
            ) -> Gst.PadProbeReturn:
                nonlocal infer_has_perception_meta
                api = GObject.type_from_name("com_arm_pek_meta_PerceptionAPI_v1")
                buffer = info.get_buffer()
                infer_has_perception_meta = bool(
                    api and buffer is not None and buffer.get_meta(api) is not None
                )
                buffer_forwarded.set()
                return Gst.PadProbeReturn.OK

            pipeline.get_by_name("infer").get_static_pad("src").add_probe(
                # Observe pekinfer directly: downstream elements retain their default
                # QoS behavior and may legitimately drop the buffer later.
                Gst.PadProbeType.BUFFER,
                observe_infer_buffer,
                None,
            )

            drain_pad = drain.get_static_pad("sink")

            def push_buffer(pts: int) -> None:
                nonlocal infer_has_perception_meta
                buffer_forwarded.clear()
                infer_has_perception_meta = False
                buffer = Gst.Buffer.new_allocate(None, 16 * 16 * 4, None)
                buffer.pts = pts
                buffer.duration = Gst.SECOND // 30
                self.assertEqual(source.emit("push-buffer", buffer), Gst.FlowReturn.OK)
                self.assertTrue(
                    buffer_forwarded.wait(1),
                    "pekinfer did not forward the video frame",
                )

            self.assertNotEqual(
                pipeline.set_state(Gst.State.PLAYING),
                Gst.StateChangeReturn.FAILURE,
            )
            try:
                pipeline.get_state(Gst.SECOND)

                # An inactive inference element must not claim the event.
                infer_sink_pad = pipeline.get_by_name("infer").get_static_pad("sink")
                self.assertTrue(
                    infer_sink_pad.push_event(
                        Gst.Event.new_qos(Gst.QOSType.UNDERFLOW, 0.75, 1, 0)
                    )
                )
                self.assertEqual(forwarded, 1, "inactive pekinfer did not forward QoS")

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
                    forwarded,
                    1,
                    "active pekinfer forwarded QoS to an upstream video element",
                )

                # The 5 ms lateness has already recovered by the next 30 FPS frame,
                # so this frame executes the 50 ms Delay Op rather than being skipped.
                frame_duration = Gst.SECOND // 30
                push_buffer(frame_duration)
                self.assertIsNone(
                    pipeline.get_bus().timed_pop_filtered(
                        100 * Gst.MSECOND,
                        Gst.MessageType.QOS | Gst.MessageType.ERROR,
                    ),
                    "a recoverable timing spike incorrectly skipped inference",
                )

                # That 50 ms execution crossed one later frame deadline. The next
                # buffer is forwarded without running the Delay Op and reports QoS.
                push_buffer(frame_duration + 1)
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
                push_buffer(frame_duration + 2)
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
                push_buffer(skipped_frame_pts)
                self.assertTrue(
                    infer_has_perception_meta,
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

                push_buffer(event_timestamp + 2 * frame_duration)
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
                infer.set_property("qos-enabled", False)
                infer.set_property("qos-enabled", True)
                push_buffer(event_timestamp + 3 * frame_duration)
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
