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

"""Exercise real MobileGaze inference, subject references, overlay and transport."""

import base64
import json
import math
import os
from pathlib import Path
import sys
import tempfile
import unittest

from file_pipeline_test_support import run_file_pipeline
from open_perception_kit.packet import decode
from open_perception_kit.fb.open_perception_kit.metadata.BoxDetections import BoxDetectionsT
from open_perception_kit.fb.open_perception_kit.metadata.FrameContext import FrameContextT
from open_perception_kit.fb.open_perception_kit.metadata.GazeMeasurements import GazeMeasurementsT


class GazePipelineTest(unittest.TestCase):
    def test_face_crop_produces_typed_gaze_for_the_explicit_subject(self):
        repository = Path(__file__).resolve().parents[2]
        model = repository / "config/models/mobilegaze-mobilenet-v2/model.json"
        if not (model.parent / json.loads(model.read_text())["modelFile"]).is_file():
            self.skipTest("MobileGaze model unavailable; use opk-models to download it")
        plugins = str(Path(sys.argv[1]).resolve())
        environment = os.environ.copy()
        environment["GST_PLUGIN_PATH"] = plugins
        environment["LD_LIBRARY_PATH"] = os.pathsep.join(filter(None, [plugins, environment.get("LD_LIBRARY_PATH")]))
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            script = directory / "seed_subject.py"
            script.write_text('''from open_perception_kit.fb.open_perception_kit.metadata.FrameContext import FrameContextT
from open_perception_kit.fb.open_perception_kit.metadata.BoxDetections import BoxDetectionsT
from open_perception_kit.fb.open_perception_kit.metadata.BoxDetection import BoxDetectionT
from open_perception_kit.fb.open_perception_kit.metadata.BoundingBox import BoundingBoxT
from open_perception_kit.fb.open_perception_kit.metadata.ImageSpace import ImageSpaceT
from open_perception_kit.fb.open_perception_kit.metadata.ObjectMeta import ObjectMetaT
from open_perception_kit.fb.open_perception_kit.metadata.LayerInfo import LayerInfoT
def process(env, tensors, context):
    frame = next(item.video for item in env.for_each(FrameContextT) if item.video is not None)
    env.add(BoxDetectionsT(layer=LayerInfoT(contentType="humanFace", producer=context.producer_info), detections=[
        BoxDetectionT(object=ObjectMetaT(id=4242), box=BoundingBoxT(x=16, y=16, width=32, height=32,
            space=ImageSpaceT(frameId=frame.object.id, width=64, height=64, extentWidth=64, extentHeight=64)))]))
''', encoding="utf-8")
            ops = []
            for loop in (0, 1):
                loop_fields = {"loopId": 1} if loop else {}
                ops.extend([
                    {"id": "opk-std-ops/InferenceController", **loop_fields,
                     "attributes": {"contentType": "humanFace"} if loop else {}},
                    {"id": "opk-std-ops/GenericImagePreprocess", **loop_fields,
                     "attributes": {"inputImageTensorIndex": 0, "inputImageSourceName": "pipelineVideoFrame"}},
                    {"id": "opk-onnx-ops/Inference", **loop_fields,
                     "attributes": {"modelDescriptor": str(model), "intraOpNumThreads": 1, "interOpNumThreads": 1}},
                ])
                ops.append({"id": "opk-std-ops/GenericPostprocess", "loopId": 1,
                            "attributes": {"parser": "GazeDetectionParser", "angleBinWidthDeg": 4}} if loop else
                           {"id": "opk-python-ops/PythonScript", "instanceId": "seed-subject",
                            "attributes": {"script": str(script)}})
            opchain = directory / "opchain.json"
            opchain.write_text(json.dumps({"version": "1.0.0", "name": "Gaze regression",
                                           "description": "Typed face-crop gaze", "ops": ops}), encoding="utf-8")
            output = directory / "frames.ndjson"
            process = run_file_pipeline([
                "videotestsrc", "num-buffers=2", "pattern=ball", "!", "video/x-raw,format=BGRA,width=64,height=64", "!",
                "opkinfer", "active=true", f"opchain-path={opchain}", "!", "opkosd", "enabled=true", "!",
                "opkcomm", "method=file", f"file-name={output}", "!", "appsink", "wait-on-eos=true", "sync=false", "async=false",
            ], output, 2, env=environment, cwd=repository)
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
            records = [json.loads(line) for line in output.read_text().splitlines() if line]
            self.assertEqual(len(records), 2)
            for record in records:
                envelope = decode(base64.b64decode(record["frame_results_packet_b64"], validate=True))
                self.assertTrue(envelope.valid(), envelope.error())
                payloads = list(envelope.for_each(GazeMeasurementsT))
                self.assertEqual(len(payloads), 1)
                self.assertEqual(len(payloads[0].measurements), 1)
                gaze = payloads[0].measurements[0]
                self.assertEqual(gaze.subjectId, 4242)
                self.assertEqual(gaze.object.parentId, 4242)
                self.assertIsNotNone(gaze.angles)
                for angle in (gaze.angles.yawDegrees, gaze.angles.pitchDegrees):
                    self.assertTrue(math.isfinite(angle))
                    self.assertGreaterEqual(angle, -180)
                    self.assertLessEqual(angle, 176)
                self.assertGreater(gaze.confidence, 0)
                self.assertLessEqual(gaze.confidence, 1)
                subjects = [item for payload in envelope.for_each(BoxDetectionsT) for item in payload.detections]
                self.assertEqual([item.object.id for item in subjects], [gaze.subjectId])
                frames = list(envelope.for_each(FrameContextT))
                self.assertEqual(subjects[0].box.space.frameId, frames[0].video.object.id)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
