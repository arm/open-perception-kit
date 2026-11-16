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

import base64
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from file_pipeline_test_support import run_file_pipeline
from open_perception_kit.packet import decode
from open_perception_kit.fb.open_perception_kit.metadata.FrameContext import FrameContextT


class AudioMetadataPipelineTest(unittest.TestCase):
    def test_original_buffer_context_is_not_a_fabricated_inference_window(self):
        plugins = str(Path(sys.argv[1]).resolve())
        environment = os.environ.copy()
        environment["GST_PLUGIN_PATH"] = plugins
        environment["LD_LIBRARY_PATH"] = os.pathsep.join(filter(None, [plugins, environment.get("LD_LIBRARY_PATH")]))
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            trace = directory / "windows.txt"
            descriptor = directory / "opchain.json"
            descriptor.write_text(json.dumps({"version": "1.0.0", "name": "audio-source-test",
                                              "description": "Original buffers remain distinct from normalized windows",
                                              "ops": [{"id": "opk-test-audio-window/Window", "attributes": {"trace": str(trace), "window": 512}}]}))
            output = directory / "results.ndjson"
            process = run_file_pipeline([
                "audiotestsrc", "num-buffers=2", "samplesperbuffer=256", "wave=sine", "!",
                "audio/x-raw,format=F32LE,rate=16000,channels=1,layout=interleaved", "!",
                "opkinfer", "name=audio", f"opchain-path={descriptor}", "!", "opkcomm", "method=file", f"file-name={output}", "!",
                "appsink", "wait-on-eos=true", "sync=false", "async=false",
            ], output, 2, env=environment, cwd=Path(__file__).resolve().parents[2])
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
            records = [json.loads(line) for line in output.read_text().splitlines() if line]
            self.assertEqual(len(records), 2)
            for index, record in enumerate(records):
                envelope = decode(base64.b64decode(record["frame_results_packet_b64"]))
                payloads = list(envelope.for_each(FrameContextT))
                self.assertEqual(len(payloads), 1)
                source = payloads[0].audio
                self.assertEqual(source.originalChannels, 1)
                self.assertEqual(source.originalFrequency, 16000)
                self.assertEqual(source.originalSampleCount, 256)
                self.assertEqual(source.presentationTsNs, index * 16000000)
                self.assertIsNotNone(payloads[0].layer.producer)
            self.assertEqual(len(trace.read_text().splitlines()), 1)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
