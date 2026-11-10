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
from open_perception_kit.fb.open_perception_kit.metadata.PoseEstimations import PoseEstimationsT


class FaceGazePipeline(unittest.TestCase):
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
                deadline = time.monotonic() + 5
                while not output.exists() or not output.read_bytes().endswith(b"\n"):
                    self.assertLess(time.monotonic(), deadline, "opkcomm output timed out")
                    time.sleep(0.01)
                records = [json.loads(line) for line in output.read_text().splitlines()]
                self.assertEqual(len(records), 1)
                envelope = decode(base64.b64decode(records[0]["frame_results_packet_b64"], validate=True))
                self.assertTrue(envelope.valid(), envelope.error())
                boxes = [box for layer in envelope.for_each(BoxDetectionsT) for box in layer.detections]
                poses = [pose for layer in envelope.for_each(PoseEstimationsT) for pose in layer.poses]
                return boxes, poses
            finally:
                release_pipeline(pipeline, gst)

    def test_original_models_produce_faces_and_linked_gaze(self):
        build = Path(sys.argv[1]).resolve()
        image = Path(sys.argv[2]).resolve()
        root = Path(__file__).resolve().parents[2]
        self.libraries = [ctypes.CDLL(str(build / name)) for name in
                          ("opk-std-ops.so", "opk-onnx-ops.so")]
        gst = load_gstreamer_plugins([build / "libopkinfer.so", build / "libopkcomm.so"])
        for width in (1080, 1920):
            for backbone in (34, 50):
                name = f"retinaface-{width}x1080-gaze-resnet{backbone}"
                chain = root / "config/opchains" / name / "opchain.json"
                with self.subTest(pipeline=name):
                    boxes, poses = self.run_pipeline(gst, chain, f'filesrc location="{image}" ! decodebin')
                    self.assertGreater(len(boxes), 0, "test image must contain detectable faces")
                    self.assertEqual(len(poses), len(boxes))
                    face_ids = {box.object.id for box in boxes}
                    self.assertEqual(len(face_ids), len(boxes))
                    self.assertNotIn(0, face_ids)
                    self.assertEqual({pose.object.parentId for pose in poses}, face_ids)
                    for box in boxes:
                        self.assertGreater(box.box.width, 0)
                        self.assertGreater(box.box.height, 0)
                    for pose in poses:
                        for angle in (pose.yaw, pose.pitch):
                            self.assertTrue(math.isfinite(angle))
                            self.assertGreaterEqual(angle, -180)
                            self.assertLessEqual(angle, 176)
                    print(f"{name}: {len(boxes)} faces, {len(poses)} linked gaze results", flush=True)
        chain = root / "config/opchains/retinaface-1080x1080-gaze-resnet34/opchain.json"
        boxes, poses = self.run_pipeline(
            gst, chain, "videotestsrc num-buffers=1 pattern=black ! video/x-raw,width=640,height=480")
        self.assertEqual(boxes, [])
        self.assertEqual(poses, [])
        print("black frame: no face or gaze results", flush=True)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
