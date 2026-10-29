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

"""Opt-in: compare CPU and zero-copy Vulkan preprocessing with real MLWL models.

Usage: python mlwl_vulkan_pipeline_test.py BUILD MODEL_JSON [MODEL_JSON ...]
Requires the prepared GPU runtime and VGF files; no conversion or downloads.
"""

import ctypes
import json
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np
from pipeline_test_utils import load_gstreamer_plugins, release_pipeline


class VulkanInputPipeline(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = Path(sys.argv[1]).resolve()
        cls.libraries = [ctypes.CDLL(str(cls.build / name)) for name in
                         ("opk-mlwl-ops.so", "opk-gpu-ops.so", "opk-python-ops.so")]
        cls.Gst = load_gstreamer_plugins([cls.build / "libopkinfer.so"])
        plugin = cls.Gst.ElementFactory.find("opkinfer").get_plugin()
        if Path(plugin.get_filename()) != cls.build / "libopkinfer.so":
            raise RuntimeError("incorrect opkinfer plugin loaded")

    def run_model(self, directory, model, mode):
        descriptor = json.loads(model.read_text())
        tensor = descriptor["inputTensors"][0]
        self.assertEqual(tensor["dataKind"], "ImageRgbChw")
        _, _, height, width = tensor["shape"]
        observer = directory / "observe.py"
        observer.write_text('''import numpy as np
from pathlib import Path
from open_perception_kit.guest import Envelope
from opk_python_ops import Context, Tensor, python_script

@python_script
def process(env: Envelope, tensors: tuple[Tensor, ...], context: Context) -> None:
    if not tensors or not all(np.isfinite(t.array).all() for t in tensors):
        raise RuntimeError("missing or non-finite inference output")
    np.savez(Path(__file__).with_suffix(".npz"), *[t.array for t in tensors])
''')
        chain = directory / "opchain.json"
        chain.write_text(json.dumps({
            "version": "1.5.0", "name": "vulkan-input-test",
            "description": "Compare preprocessing on the same MLWL runtime",
            "execution": {"preprocessing": mode},
            "ops": [
                {"id": "opk-std-ops/InferenceController", "attributes": {}},
                {"id": "opk-std-ops/GenericImagePreprocess", "attributes": {}},
                {"id": "opk-mlwl-ops/Inference",
                 "attributes": {"modelDescriptor": str(model)}},
                {"id": "opk-python-ops/PythonScript",
                 "attributes": {"script": str(observer)}},
            ],
        }))
        gst = self.Gst
        pipeline = gst.parse_launch(
            'appsrc name=src format=time caps="video/x-raw,format=BGRA,'
            f'width={width},height={height},framerate=1/1" ! '
            f'opkinfer name=infer opchain-path="{chain}" qos=false ! '
            'appsink name=sink sync=false async=false')
        outputs = []
        try:
            self.assertNotEqual(pipeline.set_state(gst.State.PLAYING),
                                gst.StateChangeReturn.FAILURE, "pipeline startup failed")
            for frame, rgb in enumerate(((255, 0, 127), (0, 255, 63), (255, 0, 127))):
                observer.with_suffix(".npz").unlink(missing_ok=True)
                data = bytes((*reversed(rgb), 255)) * (width * height)
                buffer = gst.Buffer.new_allocate(None, len(data), None)
                buffer.fill(0, data)
                buffer.pts, buffer.duration = frame * gst.SECOND, gst.SECOND
                self.assertEqual(pipeline.get_by_name("src").emit("push-buffer", buffer),
                                 gst.FlowReturn.OK)
                sample = pipeline.get_by_name("sink").emit("try-pull-sample", 180 * gst.SECOND)
                error = pipeline.get_bus().pop_filtered(gst.MessageType.ERROR)
                self.assertIsNone(error, str(error.parse_error()) if error else "")
                self.assertIsNotNone(sample, f"frame {frame} timed out")
                self.assertEqual(sample.get_buffer().pts, frame * gst.SECOND)
                self.assertTrue(observer.with_suffix(".npz").is_file(), "frame has no inference output")
                with np.load(observer.with_suffix(".npz")) as tensors:
                    outputs.append([tensors[name].copy() for name in tensors.files])
                print(f"{model.parent.name} {mode}: frame {frame} complete", flush=True)
        finally:
            release_pipeline(pipeline, gst)
        return outputs

    def test_shared_input_matches_cpu_preprocessing(self):
        for path in sys.argv[2:]:
            model = Path(path).resolve()
            with self.subTest(model=model.parent.name), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                reference = self.run_model(directory, model, "cpu")
                result = self.run_model(directory, model, "gpu")
                self.assertTrue(any(not np.array_equal(a, b)
                                    for a, b in zip(reference[0], reference[1])),
                                "reference must change with input")
                for expected_frame, actual_frame in zip(reference, result):
                    self.assertEqual(len(actual_frame), len(expected_frame))
                    for expected, actual in zip(expected_frame, actual_frame):
                        np.testing.assert_allclose(actual, expected, rtol=1e-4, atol=1e-5)
                for first, repeated in zip(result[0], result[2]):
                    np.testing.assert_array_equal(first, repeated)
                # A new pipeline must not retain device or input state from the previous one.
                restarted = self.run_model(directory, model, "gpu")
                for first, repeated in zip(result[0], restarted[0]):
                    np.testing.assert_array_equal(first, repeated)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    unittest.main(argv=[sys.argv[0]])
