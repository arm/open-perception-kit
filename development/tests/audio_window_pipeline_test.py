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

"""Exercise PCM window scheduling through a real opkinfer and a model-shaped test Op."""
import array
import ctypes
import json
from pathlib import Path
import queue
import sys
import tempfile
import unittest

from pipeline_test_utils import load_gstreamer_plugins, release_pipeline

OP_PATH = Path(sys.argv[1]).resolve()
PLUGIN_PATH = Path(sys.argv[2]).resolve()


class AudioWindowTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.library = ctypes.CDLL(str(OP_PATH))
        cls.Gst = load_gstreamer_plugins([PLUGIN_PATH])

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="opk-audio-window-")
        self.addCleanup(self.directory.cleanup)
        self.trace = Path(self.directory.name) / "trace.txt"
        self.pipeline = None
        self.addCleanup(lambda: release_pipeline(self.pipeline, self.Gst))
        self.delivered = queue.Queue()
        self.offset = 0
        self.pts_base = 0

    def create(self, hop=0, window=512, rate=16000, extra=None, second_window=None):
        attributes = {"trace": str(self.trace), "window": window, **(extra or {})}
        ops = [{"id": "opk-test-audio-window/Window", "attributes": attributes}]
        if second_window is not None:
            ops.append({"id": "opk-test-audio-window/Window", "attributes": {
                **attributes, "window": second_window}})
        descriptor = Path(self.directory.name) / "opchain.json"
        descriptor.write_text(json.dumps({"version": "1.0.0", "name": "audio-window-test",
                                          "displayName": "Audio Window Test", "task": "Test", "runtime": "Test",
                                          "description": "Model-free audio scheduler test", "ops": ops}))
        self.rate = rate
        self.pipeline = self.Gst.parse_launch(
            f'appsrc name=source format=time caps="audio/x-raw,format=F32LE,rate={rate},channels=1,layout=interleaved" '
            f'! opkinfer name=infer opchain-path="{descriptor}" audio-hop-samples={hop} qos-enabled=true '
            '! fakesink name=sink sync=false signal-handoffs=true')
        self.pipeline.get_by_name("sink").connect("handoff", lambda *args: self.delivered.put(True))
        self.source = self.pipeline.get_by_name("source")
        self.pipeline.set_state(self.Gst.State.PLAYING)

    def push(self, count, discontinuity=False, wait=True, missing_pts=False):
        data = array.array("f", range(self.offset, self.offset + count)).tobytes()
        buffer = self.Gst.Buffer.new_allocate(None, len(data), None)
        buffer.fill(0, data)
        buffer.pts = (self.Gst.CLOCK_TIME_NONE if missing_pts else
                      self.pts_base + self.offset * self.Gst.SECOND // self.rate)
        buffer.duration = count * self.Gst.SECOND // self.rate
        if discontinuity:
            buffer.set_flags(self.Gst.BufferFlags.DISCONT)
        self.offset += count
        self.assertEqual(self.source.emit("push-buffer", buffer), self.Gst.FlowReturn.OK)
        if wait:
            self.delivered.get(timeout=5)

    def rows(self):
        if not self.trace.exists():
            return []
        return [[float(value) for value in line.split()[:5]] for line in self.trace.read_text().splitlines()]

    def test_burst_and_carry(self):
        self.create()
        self.push(1500)
        self.assertEqual(self.rows(), [[0, 512, 0, 511, 1], [512, 512, 512, 1023, 0]])
        self.push(35)
        self.assertEqual(len(self.rows()), 2)
        self.push(1)
        self.assertEqual(self.rows()[-1], [1024, 512, 1024, 1535, 0])

    def test_startup_and_overlap(self):
        self.create(hop=256)
        self.push(511)
        self.assertEqual(self.rows(), [])
        self.push(1)
        self.push(512)
        self.assertEqual(self.rows(), [[0, 512, 0, 511, 1], [256, 512, 256, 767, 0], [512, 512, 512, 1023, 0]])

    def test_window_comes_from_model(self):
        self.create(window=320)
        self.push(1000)
        self.assertEqual([row[:2] for row in self.rows()], [[0, 320], [320, 320], [640, 320]])

    def test_counts_converted_samples(self):
        self.create(rate=48000)
        self.push(1500)
        self.assertEqual(self.rows(), [])
        self.push(36)
        self.assertEqual([row[:2] for row in self.rows()], [[0, 512]])

    def test_discontinuity_discards_partial_window(self):
        self.create()
        self.push(1500)
        self.push(36, discontinuity=True)
        self.assertEqual(len(self.rows()), 2)
        self.push(476)
        self.assertEqual(self.rows()[-1], [0, 512, 1500, 2011, 1])

    def test_inactive_period_discards_partial_window(self):
        self.create()
        self.push(511)
        infer = self.pipeline.get_by_name("infer")
        infer.set_property("active", False)
        self.push(512)
        infer.set_property("active", True)
        self.push(512)
        self.assertEqual(self.rows(), [[0, 512, 1023, 1534, 1]])

    def test_invalid_audio_requirements_fail_negotiation(self):
        for options in [{"hop": 513}, {"window": 0}, {"window": -1},
                        {"extra": {"batch": 2}}, {"extra": {"logmel": True}},
                        {"second_window": 320}, {"window": 960001}]:
            with self.subTest(options=options):
                self.create(**options)
                self.push(512, wait=False)
                self.source.emit("end-of-stream")
                msg = self.pipeline.get_bus().timed_pop_filtered(
                    5 * self.Gst.SECOND, self.Gst.MessageType.ERROR | self.Gst.MessageType.EOS)
                self.assertIsNotNone(msg)
                self.assertEqual(msg.type, self.Gst.MessageType.ERROR)
                release_pipeline(self.pipeline, self.Gst)
                self.assertEqual(self.rows(), [])

    def test_timestamps_follow_windows_not_incoming_buffer(self):
        self.create()
        self.pts_base = 1_000_000_000
        self.push(1500)
        timing = [line.split()[5:] for line in self.trace.read_text().splitlines()]
        self.assertEqual(timing, [["512", "1000000000", "1032000000"],
                                  ["512", "1032000000", "1064000000"]])

    def test_resampling_delay_is_compensated(self):
        self.create(rate=48000)
        self.pts_base = 1_000_000_000
        self.push(1536)
        self.assertEqual(self.trace.read_text().split()[5:],
                         ["512", "998916667", "1030916667"])

    def test_missing_pts_stays_absent(self):
        self.create()
        self.push(512, missing_pts=True)
        self.assertEqual(self.trace.read_text().split()[5:], ["512", "none", "none"])

    def test_unmarked_timestamp_gap_resets_history(self):
        self.create()
        self.push(1500)
        self.pts_base = 5_000_000_000
        self.push(36)
        self.assertEqual(len(self.rows()), 2)
        self.push(476)
        self.assertEqual(self.rows()[-1], [0, 512, 1500, 2011, 1])
        self.assertEqual(self.trace.read_text().splitlines()[-1].split()[6:],
                         ["5093750000", "5125750000"])

    def test_overrun_is_reported_instead_of_skipping_audio(self):
        self.create()
        self.push(960001, wait=False)
        msg = self.pipeline.get_bus().timed_pop_filtered(5 * self.Gst.SECOND, self.Gst.MessageType.ERROR)
        self.assertIsNotNone(msg)
        self.assertIn("overwrote", msg.parse_error()[0].message)
        self.assertEqual(self.rows(), [])


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
