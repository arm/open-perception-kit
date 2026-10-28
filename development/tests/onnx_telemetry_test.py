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

"""Check the native ONNX startup path without ORT's automatic CI opt-out."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

PLUGIN_DIRECTORY = Path(sys.argv[1]).resolve().parent
REPOSITORY = Path(__file__).resolve().parents[2]


class OnnxTelemetryTest(unittest.TestCase):
    def test_initialization_does_not_create_telemetry_storage(self):
        for configured in (None, "0"):
            with self.subTest(configured=configured), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                model = json.loads(
                    (REPOSITORY / "config/models/yolo26n-320/model.json").read_text()
                )
                model["modelFile"] = "missing.onnx"
                model.pop("hfDownload", None)
                (directory / "model.json").write_text(json.dumps(model))
                opchain = json.loads(
                    (REPOSITORY / "config/models/yolo26n-320/opchain.json").read_text()
                )
                opchain["ops"] = [op for op in opchain["ops"]
                                  if op["id"] == "opk-onnx-ops/Inference"]
                descriptor = directory / "opchain.json"
                descriptor.write_text(json.dumps(opchain))

                # Inherited CI flags would suppress upstream telemetry and hide this regression.
                environment = {
                    "PATH": os.defpath,
                    "HOME": temporary,
                    "XDG_CACHE_HOME": str(directory / "cache"),
                    "GST_REGISTRY": str(directory / "registry.bin"),
                    "GST_PLUGIN_PATH": str(PLUGIN_DIRECTORY),
                    "LD_LIBRARY_PATH": os.pathsep.join(
                        (str(PLUGIN_DIRECTORY), os.environ.get("LD_LIBRARY_PATH", ""))
                    ),
                }
                if configured is not None:
                    environment["ORT_DISABLE_TELEMETRY"] = configured
                result = subprocess.run(
                    [shutil.which("gst-launch-1.0"), "-q", "videotestsrc", "num-buffers=1",
                     "!", "opkinfer", f"opchain-path={descriptor}", "!", "fakesink"],
                    env=environment, capture_output=True, text=True, timeout=20,
                )
                self.assertNotEqual(result.returncode, 0)
                # A missing model proves Ort::Env was created before model loading failed.
                self.assertIn("File doesn't exist", result.stdout + result.stderr)
                self.assertEqual(list(directory.rglob("deviceid")), [])
                self.assertEqual(list(directory.rglob("onnxruntime.db")), [])


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
