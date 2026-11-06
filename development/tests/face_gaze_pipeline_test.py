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

"""Opt-in ONNX integration test: <meson-out directory> <image containing faces>."""

import base64
import ctypes
import json
import math
from pathlib import Path
import sys
import tempfile
import time
import unittest

from pipeline_test_utils import load_gstreamer_plugins, release_pipeline
from open_perception_kit.packet import decode
from open_perception_kit.fb.open_perception_kit.metadata.BoxDetections import BoxDetectionsT
from open_perception_kit.fb.open_perception_kit.metadata.GazeMeasurements import GazeMeasurementsT


class FaceGazePipeline(unittest.TestCase):
    def setUp(self):
        self.build = Path(sys.argv[1]).resolve()
        self.image = Path(sys.argv[2]).resolve()
        self.root = Path(__file__).resolve().parents[2]
        self.libraries = [ctypes.CDLL(str(self.build / name)) for name in
                          ("opk-std-ops.so", "opk-onnx-ops.so")]
        self.gst = load_gstreamer_plugins([self.build / "libopkinfer.so", self.build / "libopkcomm.so"])

    def read_result(self, output, count):
        deadline = time.monotonic() + 180
        while True:
            lines = output.read_text().splitlines(keepends=True) if output.exists() else []
            if len(lines) >= count and lines[count - 1].endswith("\n"):
                self.assertEqual(len(lines), count)
                record = json.loads(lines[-1])
                envelope = decode(base64.b64decode(record["frame_results_packet_b64"], validate=True))
                self.assertTrue(envelope.valid(), envelope.error())
                return envelope
            self.assertLess(time.monotonic(), deadline, "opkcomm output timed out")
            time.sleep(0.01)

    def assert_linked_gaze(self, envelope):
        boxes = [box for layer in envelope.for_each(BoxDetectionsT) for box in layer.detections]
        gazes = [gaze for layer in envelope.for_each(GazeMeasurementsT) for gaze in layer.measurements]
        self.assertGreater(len(boxes), 0, "test image must contain detectable faces")
        self.assertEqual(len(gazes), len(boxes))
        face_ids = {box.object.id for box in boxes}
        self.assertEqual(len(face_ids), len(boxes))
        self.assertNotIn(0, face_ids)
        self.assertEqual({gaze.subjectId for gaze in gazes}, face_ids)
        for box in boxes:
            self.assertIsNotNone(box.box.space)
            self.assertNotEqual(box.box.space.frameId, 0)
            self.assertGreater(box.box.space.width, 0)
            self.assertGreater(box.box.space.height, 0)
            self.assertGreater(box.box.width, 0)
            self.assertGreater(box.box.height, 0)
        for gaze in gazes:
            self.assertIsNotNone(gaze.angles)
            for angle in (gaze.angles.yawDegrees, gaze.angles.pitchDegrees):
                self.assertTrue(math.isfinite(angle))
                self.assertGreaterEqual(angle, -180)
                self.assertLessEqual(angle, 176)
        return boxes, gazes

    def run_pipeline(self, gst, chain, source):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "results.ndjson"
            pipeline = gst.parse_launch(
                source + " ! videoconvert ! video/x-raw,format=BGRA ! "
                f'opkinfer opchain-path="{chain}" ! '
                f'opkcomm method=file file-name="{output}" ! fakesink sync=false')
            try:
                pipeline.set_state(gst.State.PLAYING)
                message = pipeline.get_bus().timed_pop_filtered(
                    180 * gst.SECOND, gst.MessageType.ERROR | gst.MessageType.EOS)
                self.assertIsNotNone(message, "pipeline timed out")
                if message.type == gst.MessageType.ERROR:
                    self.fail(str(message.parse_error()))
                self.assertEqual(message.type, gst.MessageType.EOS)
                return self.read_result(output, 1)
            finally:
                release_pipeline(pipeline, gst)

    def test_original_models_produce_faces_and_linked_gaze(self):
        gst, root, image = self.gst, self.root, self.image
        for width in (1080, 1920):
            for backbone in (34, 50):
                name = f"retinaface-{width}x1080-gaze-resnet{backbone}"
                chain = root / "config/opchains" / name / "opchain.json"
                with self.subTest(pipeline=name):
                    envelope = self.run_pipeline(gst, chain, f'filesrc location="{image}" ! decodebin')
                    boxes, gazes = self.assert_linked_gaze(envelope)
                    print(f"{name}: {len(boxes)} faces, {len(gazes)} linked gaze results", flush=True)
        chain = root / "config/opchains/retinaface-1080x1080-gaze-resnet34/opchain.json"
        envelope = self.run_pipeline(
            gst, chain, "videotestsrc num-buffers=1 pattern=black ! video/x-raw,width=640,height=480")
        self.assertFalse(any(layer.detections for layer in envelope.for_each(BoxDetectionsT)))
        self.assertFalse(any(layer.measurements for layer in envelope.for_each(GazeMeasurementsT)))
        print("black frame: no face or gaze results", flush=True)

    def test_switch_combinations_in_shared_pipeline(self):
        gst = self.gst
        decoder = gst.parse_launch(
            f'filesrc location="{self.image}" ! decodebin ! videoconvert ! '
            'video/x-raw,format=BGRA ! appsink name=decoded sync=false')
        try:
            decoder.set_state(gst.State.PLAYING)
            sample = decoder.get_by_name("decoded").emit("try-pull-sample", 30 * gst.SECOND)
            self.assertIsNotNone(sample, "could not decode test image")
        finally:
            release_pipeline(decoder, gst)

        preset = json.loads((self.root / "config/pipelines/retinaface-gaze-onnx.json").read_text())
        stages = [stage.replace("${OPK_PROJECT_ROOT:-/work}", str(self.root))
                  for stage in preset["pipeline"] if stage.startswith("opkinfer ")]
        self.assertEqual(len(stages), 4)
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "results.ndjson"
            pipeline = gst.parse_launch(
                'appsrc name=source format=time ! ' + ' '.join(stages) +
                f' opkcomm method=file file-name="{output}" ! fakesink sync=false')
            source = pipeline.get_by_name("source")
            source.set_property("caps", sample.get_caps())
            infer = []
            element = source
            for _ in stages:
                element = element.get_static_pad("src").get_peer().get_parent_element()
                infer.append(element)
            self.assertEqual([element.get_property("active") for element in infer],
                             [True, False, True, False])
            try:
                pipeline.set_state(gst.State.PLAYING)
                # Return to the first combination to exercise reactivation, too.
                for frame, selected in enumerate((0, 1, 2, 3, 0)):
                    with self.subTest(combination=selected, frame=frame):
                        detector, gaze = selected // 2, 2 + selected % 2
                        if frame:
                            for element in infer:
                                element.set_property("active", False)
                            infer[detector].set_property("active", True)
                            infer[gaze].set_property("active", True)
                        self.assertEqual([element.get_property("active") for element in infer],
                                         [index in (detector, gaze) for index in range(4)])
                        buffer = sample.get_buffer().copy_deep()
                        buffer.pts = frame * gst.SECOND
                        buffer.duration = gst.SECOND
                        self.assertEqual(source.emit("push-buffer", buffer), gst.FlowReturn.OK)
                        envelope = self.read_result(output, frame + 1)
                        self.assert_linked_gaze(envelope)
                        self.assertEqual([element.get_property("active") for element in infer],
                                         [index in (detector, gaze) for index in range(4)])
                        width, backbone = [(w, b) for w in (1080, 1920) for b in (34, 50)][selected]
                        for kind, model, producer in (
                                (BoxDetectionsT, f"retinaface-resnet34-{width}x1080-fp32", detector),
                                (GazeMeasurementsT, f"gaze-resnet{backbone}-448-fp32", gaze)):
                            layers = list(envelope.for_each(kind))
                            self.assertEqual({layer.layer.inferElementId for layer in layers},
                                             {infer[producer].get_name().encode()})
                            self.assertEqual({layer.layer.model for layer in layers}, {model.encode()})
                        print(f"shared pipeline: combination {selected}, frame {frame} passed", flush=True)
                self.assertEqual(source.emit("end-of-stream"), gst.FlowReturn.OK)
                message = pipeline.get_bus().timed_pop_filtered(
                    30 * gst.SECOND, gst.MessageType.ERROR | gst.MessageType.EOS)
                self.assertIsNotNone(message, "shared pipeline timed out")
                self.assertEqual(message.type, gst.MessageType.EOS)
            finally:
                release_pipeline(pipeline, gst)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
