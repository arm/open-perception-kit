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

"""Exercise both camera-contact mappings through native inference and transport."""

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
from open_perception_kit.fb.open_perception_kit.metadata.CameraContacts import CameraContactsT
from open_perception_kit.fb.open_perception_kit.metadata.Classifications import ClassificationsT


class ClassificationPipelineTest(unittest.TestCase):
    def test_real_contact_model_preserves_mapping_and_scores(self):
        repository = Path(__file__).resolve().parents[2]
        model = repository / "config/models/nitec-resnet-18/model.json"
        if not (model.parent / json.loads(model.read_text())["modelFile"]).is_file():
            self.skipTest("NITEC model unavailable; use opk-models to download it")
        plugins = str(Path(sys.argv[1]).resolve())
        environment = os.environ.copy()
        environment["GST_PLUGIN_PATH"] = plugins
        environment["LD_LIBRARY_PATH"] = os.pathsep.join(filter(None, [plugins, environment.get("LD_LIBRARY_PATH")]))
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            paths = []
            for index in (1, 0):
                path = directory / f"opchain-contact-{index}.json"
                path.write_text(json.dumps({"version": "2.0.0", "name": "Contact representation regression",
                                            "description": "Preserve model mapping through typed contact output", "ops": [
                                                {"id": "opk-std-ops/InferenceController", "attributes": {}},
                                                {"id": "opk-std-ops/GenericImagePreprocess", "attributes": {
                                                    "inputImageTensorIndex": 0, "inputImageSourceName": "pipelineVideoFrame"}},
                                                {"id": "opk-onnx-ops/Inference", "attributes": {"modelDescriptor": str(model),
                                                                                                "intraOpNumThreads": 1, "interOpNumThreads": 1}},
                                                {"id": "opk-std-ops/GenericPostprocess", "attributes": {
                                                    "parser": "CameraContactParser", "contactClassIndex": index, "noContactClassIndex": 1 - index}},
                                            ]}), encoding="utf-8")
                paths.append(path)
            output = directory / "results.ndjson"
            process = run_file_pipeline([
                "videotestsrc", "num-buffers=2", "pattern=black", "!",
                "video/x-raw,format=BGRA,width=64,height=64", "!", "opkinfer", "name=normal", "active=true",
                f"opchain-path={paths[0]}", "!", "opkinfer", "name=reversed", "active=true",
                f"opchain-path={paths[1]}", "!", "opkcomm", "method=file", f"file-name={output}", "!",
                "appsink", "wait-on-eos=true", "sync=false", "async=false",
            ], output, 2, cwd=repository, env=environment)
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
            records = [json.loads(line) for line in output.read_text().splitlines() if line]
            self.assertEqual(len(records), 2)
            for record in records:
                envelope = decode(base64.b64decode(record["frame_results_packet_b64"], validate=True))
                self.assertTrue(envelope.valid())
                self.assertEqual(list(envelope.for_each(ClassificationsT)), [])
                payloads = list(envelope.for_each(CameraContactsT))
                self.assertEqual(len(payloads), 2)
                scores = []
                outcomes = []
                for payload in payloads:
                    self.assertEqual(len(payload.contacts), 1)
                    contact = payload.contacts[0]
                    self.assertGreater(contact.object.parentId, 0)
                    self.assertEqual(contact.subjectId, contact.object.parentId)
                    result = contact.result
                    self.assertIsNotNone(result)
                    self.assertTrue(math.isfinite(result.confidence))
                    self.assertEqual(result.modelClassIndex, 0)
                    scores.append(result.confidence)
                    outcomes.append(result.inContact)
                self.assertAlmostEqual(scores[0], scores[1], places=6)
                # Fixed black input: the pinned NITEC model chooses class 0.
                # Check this independently of agreement between the two mappings.
                self.assertEqual(outcomes, [False, True])


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
