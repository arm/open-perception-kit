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

"""Check actual bus errors, not console logs, without requiring a device."""

import ctypes
import json
from pathlib import Path
import sys
import tempfile
import unittest

from pipeline_test_utils import load_gstreamer_plugins, release_pipeline


class InferencePipelineErrors(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.libraries = [ctypes.CDLL(path) for path in sys.argv[2:]]
        cls.Gst = load_gstreamer_plugins([Path(sys.argv[1])])

    def assert_pipeline_error(self, directory, ops, cause):
        descriptor = directory / "opchain.json"
        descriptor.write_text(json.dumps({
            "version": "1.0.0",
            "name": "error-propagation",
            "description": "Pipeline error propagation test",
            "ops": ops,
        }), encoding="utf-8")
        pipeline = self.Gst.parse_launch(
            "videotestsrc num-buffers=1 ! "
            "video/x-raw,format=BGRA,width=16,height=16 ! "
            f'opkinfer name=infer opchain-path="{descriptor}" ! '
            "fakesink async=false sync=false"
        )
        try:
            pipeline.set_state(self.Gst.State.PLAYING)
            message = pipeline.get_bus().timed_pop_filtered(
                5 * self.Gst.SECOND,
                self.Gst.MessageType.ERROR | self.Gst.MessageType.EOS,
            )
            self.assertIsNotNone(message, "pipeline produced no terminal message")
            self.assertEqual(message.type, self.Gst.MessageType.ERROR)
            self.assertEqual(message.src.get_name(), "infer")
            error, debug = message.parse_error()
            self.assertIn(cause, error.message)
            self.assertNotRegex(error.message, r"\x1b|\\x1b")
            self.assertNotIn("ImageNetClassificationParser.cpp", error.message)
            self.assertIn(str(descriptor), debug)
        finally:
            release_pipeline(pipeline, self.Gst)

    def test_postprocessor_error_reaches_bus(self):
        with tempfile.TemporaryDirectory() as temporary:
            self.assert_pipeline_error(Path(temporary), [{
                "id": "opk-std-ops/GenericPostprocess",
                "attributes": {"parser": "ImageNetClassificationParser"},
            }], "ImageNetClassificationParser: input tensor is null")

    @unittest.skipUnless(len(sys.argv) > 3, "MLWL backend disabled")
    def test_mlwl_startup_error_reaches_bus(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            (directory / "invalid.vgf").write_bytes(b"not a VGF")
            (directory / "model.json").write_text(json.dumps({
                "version": "1.0.0", "name": "invalid-vgf",
                "modelFile": "invalid.vgf", "dynamicOutput": False,
                "inputTensors": [{"shape": [1], "dataKind": "RawTensorData",
                                  "valueType": "Float32"}],
                "outputTensors": [{"shape": [1], "dataKind": "RawTensorData",
                                   "valueType": "Float32"}],
            }), encoding="utf-8")
            self.assert_pipeline_error(directory, [{
                "id": "opk-mlwl-ops/Inference",
                "attributes": {"modelDescriptor": "model.json"},
            }], "MLWL startup:")


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
