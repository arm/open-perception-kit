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

"""Startup property discovery, readback, rejection, and explicit restart behavior."""

import base64
import ctypes
from pathlib import Path
import sys
import tempfile
import time
import unittest

from pipeline_test_utils import load_gstreamer_plugins, release_pipeline, write_test_opchain

DELAY_OP_PATH = Path(sys.argv[1]).resolve()
PLUGIN_PATHS = [Path(argument).resolve() for argument in sys.argv[2:]]
STATE_TIMEOUT_SECONDS = 5
FRAME_COUNT = 3
FLOAT_PLACES = 5
FLOAT_INCREMENT = 0.25
POLL_INTERVAL_SECONDS = 0.01
TCP_METHOD = 2
# One red RGB pixel, encoded as PNG (the background loader supports PNG/JPEG).
RED_PIXEL_PNG = 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCC'


class ElementPropertiesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.library = ctypes.CDLL(str(DELAY_OP_PATH))
        cls.Gst = load_gstreamer_plugins(PLUGIN_PATHS)
        from gi.repository import GObject
        cls.GObject = GObject

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="opk-properties-")
        self.addCleanup(self.directory.cleanup)
        self.image = Path(self.directory.name) / "background.png"
        self.image.write_bytes(base64.b64decode(RED_PIXEL_PNG))
        self.opchain = write_test_opchain(self.directory.name, "first", {})

    def own_properties(self, element):
        return [prop for prop in element.list_properties()
                if prop.owner_type == element.__gtype__]

    def sample_value(self, prop):
        special = {"bg-image": str(self.image), "opchain-path": str(self.opchain),
                   "association-mode": "iou", "static-files": self.directory.name,
                   "method": TCP_METHOD}
        if prop.name in special:
            return special[prop.name]
        default = prop.default_value
        if prop.value_type == self.GObject.TYPE_BOOLEAN:
            return not default
        if prop.value_type == self.GObject.TYPE_STRING:
            return "contract-test"
        if prop.value_type in (self.GObject.TYPE_FLOAT, self.GObject.TYPE_DOUBLE):
            return min(float(default) + FLOAT_INCREMENT, float(prop.maximum))
        return min(int(default) + 1, int(prop.maximum))

    def test_metadata_defaults_and_null_roundtrips(self):
        dynamic_defaults = {("opkinfer", "infer-id"), ("opktracker", "infer-id"),
                            ("opksink", "static-files"), ("opksink", "webrtc-stun-server"),
                            ("opksink", "webrtc-turn-server")}
        for path in PLUGIN_PATHS:
            factory = path.stem.removeprefix("lib")
            element = self.Gst.ElementFactory.make(factory)
            self.assertIsNotNone(element, factory)
            for prop in self.own_properties(element):
                with self.subTest(factory=factory, property=prop.name):
                    self.assertTrue(prop.flags & self.GObject.ParamFlags.READABLE)
                    current = element.get_property(prop.name)
                    if (factory, prop.name) not in dynamic_defaults:
                        self.assertEqual(current, prop.default_value)
                    if not prop.flags & self.GObject.ParamFlags.WRITABLE:
                        with self.assertRaises(TypeError):
                            element.set_property(prop.name, current)
                        continue
                    self.assertTrue("Changeable while playing" in prop.blurb or
                                    "Set in NULL" in prop.blurb, prop.blurb)
                    sample = self.sample_value(prop)
                    element.set_property(prop.name, sample)
                    actual = element.get_property(prop.name)
                    if prop.value_type == self.GObject.TYPE_FLOAT:
                        self.assertAlmostEqual(actual, sample, places=FLOAT_PLACES)
                    else:
                        self.assertEqual(actual, sample)
                    element.set_property(prop.name, current)
            if factory == "opkinfer":
                self.assertIsNone(element.find_property("format"))
            if factory == "opktracker":
                self.assertIsNone(element.find_property("embedding-content-type"))
                for name in ("embedding-space-id", "embedding-producer-id"):
                    self.assertTrue(element.find_property(name).flags & self.Gst.PARAM_MUTABLE_READY)
                    element.set_property(name, None)
                    self.assertEqual(element.get_property(name), "")

    def test_ready_only_properties_reject_during_processing_and_apply_after_stop(self):
        pipeline = self.Gst.parse_launch(
            'videotestsrc is-live=true ! video/x-raw,format=BGRA,width=16,height=16 ! '
            f'opkinfer name=infer opchain-path="{self.opchain}" ! '
            'opktracker name=tracker ! opkosd name=osd ! fakesink sync=false')
        self.addCleanup(release_pipeline, pipeline, self.Gst)
        self.assertNotEqual(pipeline.set_state(self.Gst.State.PLAYING),
                            self.Gst.StateChangeReturn.FAILURE)
        self.assertEqual(pipeline.get_state(STATE_TIMEOUT_SECONDS * self.Gst.SECOND)[1],
                         self.Gst.State.PLAYING)
        changes = []
        for name in ("infer", "tracker", "osd"):
            element = pipeline.get_by_name(name)
            for prop in self.own_properties(element):
                if not prop.flags & self.Gst.PARAM_MUTABLE_READY:
                    continue
                with self.subTest(element=name, property=prop.name):
                    current = element.get_property(prop.name)
                    candidate = self.sample_value(prop)
                    if candidate == current:
                        candidate = str(write_test_opchain(self.directory.name, "second", {}))
                    element.set_property(prop.name, candidate)
                    self.assertEqual(element.get_property(prop.name), current)
                    message = pipeline.get_bus().timed_pop_filtered(
                        self.Gst.SECOND, self.Gst.MessageType.WARNING)
                    self.assertIsNotNone(message)
                    self.assertIn(prop.name, message.parse_warning()[0].message)
                    changes.append((element, prop.name, candidate))
        pipeline.set_state(self.Gst.State.READY)
        self.assertEqual(pipeline.get_state(STATE_TIMEOUT_SECONDS * self.Gst.SECOND)[1],
                         self.Gst.State.READY)
        for element, name, candidate in changes:
            element.set_property(name, candidate)
            self.assertEqual(element.get_property(name), candidate)
        self.assertNotEqual(pipeline.set_state(self.Gst.State.PLAYING),
                            self.Gst.StateChangeReturn.FAILURE)
        self.assertEqual(pipeline.get_state(STATE_TIMEOUT_SECONDS * self.Gst.SECOND)[1],
                         self.Gst.State.PLAYING)

    def test_invalid_background_does_not_claim_effective_value(self):
        osd = self.Gst.ElementFactory.make("opkosd")
        osd.set_property("bg-image", str(self.image))
        self.assertEqual(osd.get_property("bg-image"), str(self.image))
        osd.set_property("bg-image", str(self.image) + ".missing")
        self.assertEqual(osd.get_property("bg-image"), str(self.image))
        osd.set_property("bg-image", None)
        self.assertEqual(osd.get_property("bg-image"), "")

    def test_background_removed_before_startup_reports_failure(self):
        pipeline = self.Gst.parse_launch('videotestsrc ! opkosd name=osd ! fakesink')
        self.addCleanup(release_pipeline, pipeline, self.Gst)
        pipeline.get_by_name("osd").set_property("bg-image", str(self.image))
        self.image.unlink()
        self.assertEqual(pipeline.set_state(self.Gst.State.PAUSED),
                         self.Gst.StateChangeReturn.FAILURE)
        message = pipeline.get_bus().timed_pop_filtered(self.Gst.SECOND, self.Gst.MessageType.ERROR)
        self.assertIsNotNone(message)
        self.assertIn("bg-image", message.parse_error()[0].message)

    def test_invalid_tracker_mode_is_rejected(self):
        tracker = self.Gst.ElementFactory.make("opktracker")
        tracker.set_property("association-mode", "iou")
        tracker.set_property("association-mode", "misspelled")
        self.assertEqual(tracker.get_property("association-mode"), "iou")

    def test_missing_opchain_fails_even_when_inactive(self):
        pipeline = self.Gst.parse_launch('videotestsrc ! opkinfer active=false ! fakesink')
        self.addCleanup(release_pipeline, pipeline, self.Gst)
        self.assertEqual(pipeline.set_state(self.Gst.State.PAUSED),
                         self.Gst.StateChangeReturn.FAILURE)
        message = pipeline.get_bus().timed_pop_filtered(self.Gst.SECOND,
                                                        self.Gst.MessageType.ERROR)
        self.assertIsNotNone(message)
        self.assertIn("opchain-path", message.parse_error()[0].message)

    def test_failed_live_writer_reopen_reports_error_and_keeps_requested_value(self):
        output = Path(self.directory.name) / "valid.jsonl"
        invalid = Path(self.directory.name) / "missing-directory" / "invalid.jsonl"
        pipeline = self.Gst.parse_launch(
            f'videotestsrc ! opkcomm name=comm file-name="{output}" ! fakesink sync=false')
        self.addCleanup(release_pipeline, pipeline, self.Gst)
        pipeline.set_state(self.Gst.State.PLAYING)
        self.assertEqual(pipeline.get_state(STATE_TIMEOUT_SECONDS * self.Gst.SECOND)[1],
                         self.Gst.State.PLAYING)
        comm = pipeline.get_by_name("comm")
        comm.set_property("file-name", str(invalid))
        message = pipeline.get_bus().timed_pop_filtered(self.Gst.SECOND, self.Gst.MessageType.ERROR)
        self.assertIsNotNone(message)
        self.assertEqual(message.src, comm)
        self.assertIn("file-name", message.parse_error()[1])
        self.assertEqual(comm.get_property("file-name"), str(invalid))
        # No saved working combination: the caller explicitly corrects the value.
        pipeline.set_state(self.Gst.State.NULL)
        comm.set_property("file-name", str(output))
        pipeline.set_state(self.Gst.State.PLAYING)
        self.assertEqual(pipeline.get_state(STATE_TIMEOUT_SECONDS * self.Gst.SECOND)[1],
                         self.Gst.State.PLAYING)

        errors = []

        def stop_on_writer_error(_bus, message, _data):
            if message.type == self.Gst.MessageType.ERROR and message.src == comm:
                errors.append(message.parse_error())
                pipeline.set_state(self.Gst.State.NULL)
            return self.Gst.BusSyncReply.PASS

        bus = pipeline.get_bus()
        self.addCleanup(bus.set_sync_handler, None, None)
        bus.set_sync_handler(stop_on_writer_error, None)
        # The setter's synchronous callback must be able to wait for the streaming thread.
        comm.set_property("file-name", str(invalid))
        bus.set_sync_handler(None, None)
        self.assertEqual(len(errors), 1)
        self.assertIn(str(invalid), errors[0][1])
        self.assertEqual(comm.get_property("file-name"), str(invalid))
        self.assertEqual(pipeline.get_state(STATE_TIMEOUT_SECONDS * self.Gst.SECOND)[1],
                         self.Gst.State.NULL)

    def test_file_destination_receives_records_after_explicit_restart(self):
        outputs = [Path(self.directory.name) / name for name in ("first.jsonl", "second.jsonl")]
        pipeline = self.Gst.parse_launch(
            f'videotestsrc num-buffers={FRAME_COUNT} ! opkcomm name=comm ! fakesink')
        self.addCleanup(release_pipeline, pipeline, self.Gst)
        comm = pipeline.get_by_name("comm")
        for output in outputs:
            comm.set_property("file-name", str(output))
            self.assertNotEqual(pipeline.set_state(self.Gst.State.PLAYING),
                                self.Gst.StateChangeReturn.FAILURE)
            message = pipeline.get_bus().timed_pop_filtered(
                STATE_TIMEOUT_SECONDS * self.Gst.SECOND,
                self.Gst.MessageType.ERROR | self.Gst.MessageType.EOS)
            self.assertIsNotNone(message)
            self.assertEqual(message.type, self.Gst.MessageType.EOS)
            # Existing writer shutdown may discard queued records; this test checks
            # startup destination selection, not the step-2 queue handoff contract.
            deadline = time.monotonic() + STATE_TIMEOUT_SECONDS
            while time.monotonic() < deadline and (
                    not output.exists() or len(output.read_text().splitlines()) < FRAME_COUNT):
                time.sleep(POLL_INTERVAL_SECONDS)
            self.assertEqual(len(output.read_text().splitlines()), FRAME_COUNT)
            pipeline.set_state(self.Gst.State.NULL)
        self.assertEqual(len(outputs[0].read_text().splitlines()), FRAME_COUNT)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
