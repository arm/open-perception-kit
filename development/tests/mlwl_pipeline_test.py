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

"""Opt-in real-runtime test: <meson-out directory> <model-converter executable>."""

import base64
import ctypes
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from pipeline_test_utils import load_gstreamer_plugins, release_pipeline
from open_perception_kit.packet import decode
from open_perception_kit.fb.open_perception_kit.metadata.SegmentationMasks import SegmentationMasksT


class MlwlPipeline(unittest.TestCase):
    def test_image_and_scalar_inputs_reach_frame_results(self):
        build = Path(sys.argv[1]).resolve()
        self.libraries = [ctypes.CDLL(str(build / name)) for name in
                          ("opk-std-ops.so", "opk-mlwl-ops.so")]
        gst = load_gstreamer_plugins([build / "libopkinfer.so", build / "libopkcomm.so"])
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            graph = directory / "add.mlir"
            source = '''module {
  func.func @main(%input: tensor<1x1x1x1xf32>) -> tensor<1x1x1x1xf32> {
    %offset = "tosa.const"() {values = dense<0.25> : tensor<1x1x1x1xf32>} : () -> tensor<1x1x1x1xf32>
    %output = "tosa.add"(%input, %offset) : (tensor<1x1x1x1xf32>, tensor<1x1x1x1xf32>) -> tensor<1x1x1x1xf32>
    return %output : tensor<1x1x1x1xf32>
  }
}
'''
            graph.write_text(source, encoding="utf-8")
            subprocess.run([sys.argv[2], "--input", str(graph), "--output",
                            str(directory / "add.vgf")], check=True, timeout=30)
            for kind, pattern, expected in (("ImageGray", (0, 255), (191, 0)),
                                            ("ImageGray", (255, 0), (0, 191)),
                                            ("Value", (0, 255), (63, 63))):
                with self.subTest(kind=kind, pattern=pattern):
                    tensor = {"dataKind": kind, "valueType": "Float32"}
                    if kind == "Value":
                        tensor["valueInputs"] = [0.5]
                    else:
                        tensor["shape"] = [1, 1, 1, 1]
                    (directory / "model.json").write_text(json.dumps({
                        "version": "1.0.0", "name": "mlwl-add",
                        "modelFile": "add.vgf", "dynamicOutput": False,
                        "inputTensors": [tensor],
                        "outputTensors": [{"shape": [1, 1, 1, 1],
                                           "dataKind": "RawTensorData", "valueType": "Float32"}],
                    }), encoding="utf-8")
                    ops = []
                    if kind != "Value":
                        ops = [{"id": "opk-std-ops/InferenceController", "attributes": {}},
                               {"id": "opk-std-ops/GenericImagePreprocess", "attributes": {}}]
                    ops += [{"id": "opk-mlwl-ops/Inference", "attributes": {"modelDescriptor": "model.json"}},
                            {"id": "opk-std-ops/GenericPostprocess", "attributes": {"parser": "ModNetSegmentationParser"}}]
                    descriptor = directory / "opchain.json"
                    descriptor.write_text(json.dumps({"version": "1.0.0", "name": "mlwl-pipeline-test",
                                                      "description": "Real VGF pipeline", "ops": ops}), encoding="utf-8")
                    frames = directory / "frames.bgra"
                    frames.write_bytes(b"".join(bytes((value, value, value, 255)) * 256
                                                for value in pattern))
                    output = directory / f"{kind}-{pattern[0]}.ndjson"
                    pipeline = gst.parse_launch(
                        f'filesrc location="{frames}" ! '
                        "rawvideoparse format=bgra width=16 height=16 framerate=1/1 ! "
                        f'opkinfer opchain-path="{descriptor}" ! '
                        f'opkcomm method=file file-name="{output}" ! fakesink sync=false'
                    )
                    try:
                        pipeline.set_state(gst.State.PLAYING)
                        message = pipeline.get_bus().timed_pop_filtered(
                            30 * gst.SECOND, gst.MessageType.ERROR | gst.MessageType.EOS)
                        self.assertIsNotNone(message, "pipeline timed out")
                        if message.type == gst.MessageType.ERROR:
                            self.fail(str(message.parse_error()))
                        self.assertEqual(message.type, gst.MessageType.EOS)
                    finally:
                        release_pipeline(pipeline, gst)
                    records = [json.loads(line) for line in output.read_text().splitlines()]
                    self.assertEqual(len(records), 2)
                    for record, value in zip(records, expected):
                        envelope = decode(base64.b64decode(record["frame_results_packet_b64"], validate=True))
                        self.assertTrue(envelope.valid(), envelope.error())
                        payloads = list(envelope.for_each(SegmentationMasksT))
                        self.assertEqual(len(payloads), 1)
                        self.assertEqual(len(payloads[0].masks), 1)
                        bitmap = payloads[0].masks[0].bitmap
                        self.assertEqual((bitmap.width, bitmap.height, len(bitmap.pixels)), (1, 1, 1))
                        self.assertEqual(bitmap.pixels[0], value)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
