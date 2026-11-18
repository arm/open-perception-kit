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

"""Exercise cached mask model artifacts through native inference and transport.

No downloads: optional models absent from the local artifact cache are skipped.
Fixture input sizes match the supported model exports; the backend also inspects
the ONNX tensors when loading each model.
"""

import base64
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

from file_pipeline_test_support import run_file_pipeline
from open_perception_kit.packet import decode
from open_perception_kit.fb.open_perception_kit.metadata.SegmentationMasks import SegmentationMasksT
from open_perception_kit.fb.open_perception_kit.metadata.SemanticSegmentationMasks import SemanticSegmentationMasksT
from open_perception_kit.fb.open_perception_kit.metadata.InstanceSegmentationMasks import InstanceSegmentationMasksT
from open_perception_kit.fb.open_perception_kit.metadata.OpacityMeaning import OpacityMeaning

REPOSITORY = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY / "tools/plumber"))
from plumber.distance_seg_map import valid_mask, MASK_TYPES  # noqa: E402
from plumber.frame_results_normalize import normalize_frame_results  # noqa: E402


class MaskPipelineTest(unittest.TestCase):
    def run_model(self, relative_path, parser, root, size):
        artifact = REPOSITORY / "config/models" / relative_path
        if not artifact.is_file():
            self.skipTest(f"Optional cached model unavailable: {artifact.name}")
        plugins = str(Path(sys.argv[1]).resolve())
        environment = os.environ.copy()
        environment["GST_PLUGIN_PATH"] = plugins
        environment["LD_LIBRARY_PATH"] = os.pathsep.join(filter(None, [plugins, environment.get("LD_LIBRARY_PATH")]))
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            model = directory / "model.json"
            model.write_text(json.dumps({"version": "2.0.0", "name": "Mask representation regression",
                                         "modelFile": str(artifact), "dynamicOutput": True, "inputTensors": [{"dataKind": "ImageRgbChw",
                                                                                                              "shape": [1, 3, size, size], "keepAspectRatio": True, "mean": [0, 0, 0], "std": [1, 1, 1]}]}))
            chain = directory / "opchain.json"
            chain.write_text(json.dumps({"version": "2.0.0", "name": "Mask regression",
                                         "description": "Verify mask image space and semantics through real inference", "ops": [
                                             {"id": "opk-std-ops/InferenceController", "attributes": {}},
                                             {"id": "opk-std-ops/GenericImagePreprocess", "attributes": {
                                                 "inputImageTensorIndex": 0, "inputImageSourceName": "pipelineVideoFrame"}},
                                             {"id": "opk-onnx-ops/Inference", "attributes": {"modelDescriptor": str(model),
                                                                                             "intraOpNumThreads": 1, "interOpNumThreads": 1}},
                                             {"id": "opk-std-ops/GenericPostprocess", "attributes": {"parser": parser}},
                                         ]}))
            output = directory / "results.ndjson"
            process = run_file_pipeline([
                "filesrc", f"location={REPOSITORY / 'data/images/GettyImages-1140581459-thumbnail.jpg'}", "!",
                "jpegdec", "!", "imagefreeze", "num-buffers=2", "!", "videoconvert", "!", "videoscale", "!",
                "video/x-raw,format=BGRA,width=640,height=480", "!", "opkinfer", "active=true",
                f"opchain-path={chain}", "!", "opkosd", "!", "opkcomm", "method=file", f"file-name={output}", "!",
                "appsink", "wait-on-eos=true", "sync=false", "async=false",
            ], output, 2, cwd=REPOSITORY, env=environment)
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
            records = [json.loads(line) for line in output.read_text().splitlines() if line]
            self.assertEqual(len(records), 2)
            for record in records:
                envelope = decode(base64.b64decode(record["frame_results_packet_b64"], validate=True))
                self.assertTrue(envelope.valid())
                payloads = list(envelope.for_each(root))
                self.assertEqual(len(payloads), 1)
                if root is InstanceSegmentationMasksT:
                    self.assertGreater(len(payloads[0].masks or []), 0,
                                       "Known-person input must produce instance masks")
                    self.assertTrue(any(any(mask.bitmap.pixels) for mask in payloads[0].masks),
                                    "Positive instance case must contain foreground pixels")
                else:
                    self.assertEqual(len(payloads[0].masks), 1)
                if root is SegmentationMasksT:
                    self.assertEqual(payloads[0].masks[0].meaning, OpacityMeaning.BackgroundOpacity)
                snapshot = normalize_frame_results(envelope)
                for payload in snapshot.payloads.values():
                    for item in payload.items:
                        if item["item_type"] in MASK_TYPES:
                            self.assertTrue(valid_mask(item), item["item_type"])
                            self.assertEqual(item["data"]["bitmap"]["region"]["frameWidth"], 640)
                            self.assertEqual(item["data"]["bitmap"]["region"]["frameHeight"], 480)

    def test_modnet(self):
        self.run_model("modnet/model.onnx", "ModNetSegmentationParser", SegmentationMasksT, 512)

    def test_semantic(self):
        self.run_model("yolo26n-sem/yolo26n-sem.onnx", "Yolo26SemanticSegmentationParser",
                       SemanticSegmentationMasksT, 1024)

    def test_instances(self):
        self.run_model("yolo26n-seg/yolo26n-seg.onnx", "Yolo26InstanceSegmentationParser",
                       InstanceSegmentationMasksT, 640)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
