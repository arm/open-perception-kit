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

"""Normalized ROI -> crop controller -> preprocessing/inference -> packet."""

import base64
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

from file_pipeline_test_support import run_file_pipeline

from open_perception_kit.packet import decode
from open_perception_kit.fb.open_perception_kit.metadata.BoxDetections import BoxDetectionsT
from open_perception_kit.fb.open_perception_kit.metadata.FrameContext import FrameContextT


class ImageGeometryPipelineTest(unittest.TestCase):
    def test_normalized_roi_retains_source_space_through_real_preprocessing(self):
        repository = Path(__file__).resolve().parents[2]
        model = repository / "config/models/yolo26n-320/model.json"
        if not (model.parent / "yolo26n_raspberry_onnx_optimized.onnx").is_file():
            self.skipTest("YOLO26n-320 artifact unavailable; use the model downloader")
        plugins = str(Path(sys.argv[1]).resolve())
        environment = os.environ.copy()
        environment["GST_PLUGIN_PATH"] = plugins
        environment["LD_LIBRARY_PATH"] = os.pathsep.join(filter(None, [plugins, environment.get("LD_LIBRARY_PATH")]))
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            script = directory / "seed.py"
            script.write_text('''from open_perception_kit.fb.open_perception_kit.metadata.FrameContext import FrameContextT
from open_perception_kit.fb.open_perception_kit.metadata.BoxDetections import BoxDetectionsT
from open_perception_kit.fb.open_perception_kit.metadata.BoxDetection import BoxDetectionT
from open_perception_kit.fb.open_perception_kit.metadata.BoundingBox import BoundingBoxT
from open_perception_kit.fb.open_perception_kit.metadata.ImageSpace import ImageSpaceT
from open_perception_kit.fb.open_perception_kit.metadata.CoordinateUnits import CoordinateUnits
from open_perception_kit.fb.open_perception_kit.metadata.ObjectMeta import ObjectMetaT
from open_perception_kit.fb.open_perception_kit.metadata.LayerInfo import LayerInfoT
def process(env, tensors, context):
    frame = next(item.video for item in env.for_each(FrameContextT) if item.video is not None)
    space = ImageSpaceT(frameId=frame.object.id, width=64, height=64, extentWidth=64, extentHeight=64)
    env.add(BoxDetectionsT(layer=LayerInfoT(model="seed", contentType="genericObject", producer=context.producer_info),
        detections=[BoxDetectionT(object=ObjectMetaT(id=424242, parentId=999999),
            box=BoundingBoxT(x=.25, y=.25, width=.5, height=.5, space=space, units=CoordinateUnits.Normalized))]))
''', encoding="utf-8")
            opchain = directory / "opchain.json"
            opchain.write_text(json.dumps({"version": "1.0.0", "name": "Geometry regression",
                                          "description": "Normalized ROI through native inference", "ops": [
                                              {"id": "opk-std-ops/InferenceController", "attributes": {}},
                                              {"id": "opk-std-ops/GenericImagePreprocess", "attributes": {
                                                  "inputImageTensorIndex": 0, "inputImageSourceName": "pipelineVideoFrame"}},
                                              {"id": "opk-onnx-ops/Inference",
                                                  "attributes": {"modelDescriptor": str(model)}},
                                              {"id": "opk-python-ops/PythonScript", "instanceId": "seed-roi",
                                                  "attributes": {"script": str(script)}},
                                              {"id": "opk-std-ops/InferenceController", "loopId": 1,
                                                  "attributes": {"contentType": "genericObject"}},
                                              {"id": "opk-std-ops/GenericImagePreprocess", "loopId": 1,
                                               "attributes": {"inputImageTensorIndex": 0, "inputImageSourceName": "pipelineVideoFrame"}},
                                              {"id": "opk-onnx-ops/Inference", "loopId": 1,
                                                  "attributes": {"modelDescriptor": str(model)}},
                                              {"id": "opk-std-ops/GenericPostprocess", "loopId": 1, "attributes": {
                                                  "parser": "YoloParser", "outputFormat": "cornerScoreClass", "normalizeOutputCoordinates": False,
                                                  "confidenceThreshold": 0.0, "applyNms": False}},
                                          ]}), encoding="utf-8")
            output = directory / "frames.ndjson"
            process = run_file_pipeline([
                "videotestsrc", "num-buffers=2", "pattern=ball", "!",
                "video/x-raw,format=BGRA,width=64,height=64", "!", "opkinfer", "active=true",
                f"opchain-path={opchain}", "!", "opkcomm", "method=file", f"file-name={output}", "!",
                "appsink", "wait-on-eos=true", "sync=false", "async=false",
            ], output, 2, env=environment, cwd=repository)
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
            records = [json.loads(line) for line in output.read_text().splitlines() if line]
            self.assertEqual(len(records), 2)
            for record in records:
                envelope = decode(base64.b64decode(record["frame_results_packet_b64"], validate=True))
                self.assertTrue(envelope.valid())
                frames = list(envelope.for_each(FrameContextT))
                self.assertEqual(len(frames), 1)
                detections = [item for payload in envelope.for_each(BoxDetectionsT)
                              for item in payload.detections if item.object.id != 424242]
                self.assertTrue(detections)
                for item in detections:
                    self.assertEqual(item.object.parentId, 424242)
                    self.assertEqual(item.box.space.frameId, frames[0].video.object.id)
                    self.assertEqual((item.box.space.width, item.box.space.height), (64, 64))
                    self.assertEqual((item.box.space.originX, item.box.space.originY), (16, 16))
                    self.assertEqual((item.box.space.extentWidth, item.box.space.extentHeight), (32, 32))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
