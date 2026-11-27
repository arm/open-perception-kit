#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

"""Exercise Cairn's appsink/action path through standard PyGObject bindings."""

import base64
import json
from pathlib import Path
import sys
import tempfile
import time
import unittest

from pipeline_test_utils import load_gstreamer_plugins, release_pipeline
from open_perception_kit.packet import decode
from open_perception_kit.fb.open_perception_kit.metadata.PerformanceMeasurements import PerformanceMeasurementsT


class OpkCommActionPipelineTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.Gst = load_gstreamer_plugins([Path(path).resolve() for path in sys.argv[1:]])
        from gi.repository import GLib, GObject
        cls.GLib = GLib
        cls.GObject = GObject

    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="opk-serialize-results-")
        self.addCleanup(directory.cleanup)
        self.output = Path(directory.name) / "results.jsonl"

    def pipeline(self, before="opkperformance !", after=""):
        gst = self.Gst
        pipeline = gst.parse_launch(
            'appsrc name=source is-live=true format=time '
            'caps=video/x-raw,format=BGRA,width=16,height=16,framerate=30/1 ! '
            f'{before} opkcomm name=results method=none file-name="{self.output}" ! '
            f'{after} appsink name=frames sync=false async=false wait-on-eos=false')
        self.addCleanup(release_pipeline, pipeline, gst)
        self.assertNotEqual(pipeline.set_state(gst.State.PLAYING), gst.StateChangeReturn.FAILURE)
        return pipeline

    def push_frame(self, pipeline, index=0):
        buffer = self.Gst.Buffer.new_allocate(None, 16 * 16 * 4, None)
        buffer.memset(0, 0, buffer.get_size())
        buffer.pts = index * self.Gst.SECOND // 30
        buffer.duration = self.Gst.SECOND // 30
        self.assertEqual(pipeline.get_by_name("source").emit("push-buffer", buffer),
                         self.Gst.FlowReturn.OK)
        sample = pipeline.get_by_name("frames").emit("try-pull-sample", 5 * self.Gst.SECOND)
        self.assertIsNotNone(sample)
        return sample

    def packet(self, comm, sample):
        response = comm.emit("serialize-results", sample.get_buffer())
        self.assertEqual(response.get_value("status"), "ok")
        # get_value() exposes a borrowed boxed value; retain it before response dies.
        packet = response.get_value("packet").ref()
        self.assertIsInstance(packet, self.GLib.Bytes)
        return packet  # The response structure is no longer retained by this helper.

    def test_action_only_mode_and_packet_ownership(self):
        pipeline = self.pipeline()
        comm = pipeline.get_by_name("results")
        self.assertNotEqual(self.GObject.signal_lookup("serialize-results", comm.__gtype__), 0)
        sample = self.push_frame(pipeline)
        buffer = sample.get_buffer()
        pixels = buffer.extract_dup(0, buffer.get_size())
        packet = self.packet(comm, sample)
        self.assertEqual(self.packet(comm, sample).get_data(), packet.get_data())
        self.assertEqual(buffer.extract_dup(0, buffer.get_size()), pixels)
        self.assertEqual(buffer.pts, 0)
        self.assertFalse(self.output.exists())
        del buffer, sample
        pipeline.set_state(self.Gst.State.NULL)
        envelope = decode(packet.get_data())
        self.assertTrue(envelope.valid())
        self.assertEqual(len(list(envelope.for_each(PerformanceMeasurementsT))), 1)

    def test_results_added_after_provider_are_included(self):
        pipeline = self.pipeline(after="opkperformance !")
        sample = self.push_frame(pipeline)
        packet = self.packet(pipeline.get_by_name("results"), sample)
        envelope = decode(packet.get_data())
        self.assertTrue(envelope.valid())
        self.assertEqual(len(list(envelope.for_each(PerformanceMeasurementsT))), 2)

    def test_missing_metadata(self):
        pipeline = self.pipeline(before="")
        sample = self.push_frame(pipeline)
        response = pipeline.get_by_name("results").emit("serialize-results", sample.get_buffer())
        self.assertEqual(response.get_value("status"), "no-metadata")
        self.assertFalse(response.has_field("packet"))
        self.assertTrue(response.get_value("message"))

    def test_live_writer_switch_and_action_packet_match(self):
        pipeline = self.pipeline()
        comm = pipeline.get_by_name("results")
        self.push_frame(pipeline)
        self.assertFalse(self.output.exists())

        comm.set_property("method", 1)  # Existing file enum value is preserved.
        sample = self.push_frame(pipeline, 1)
        packet = self.packet(comm, sample)
        deadline = time.monotonic() + 5
        record = ""
        while time.monotonic() < deadline:
            record = self.output.read_text() if self.output.exists() else ""
            if record.endswith("\n"):
                break
            time.sleep(0.01)
        self.assertTrue(record.endswith("\n"), "writer output timed out")
        published = json.loads(record)
        self.assertEqual(base64.b64decode(published["frame_results_packet_b64"], validate=True),
                         packet.get_data())

        comm.set_property("method", 0)  # none: subsequent frames must not be published.
        later = self.push_frame(pipeline, 2)
        self.assertTrue(decode(self.packet(comm, later).get_data()).valid())
        pipeline.set_state(self.Gst.State.NULL)
        self.assertEqual(self.output.read_text(), record)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
