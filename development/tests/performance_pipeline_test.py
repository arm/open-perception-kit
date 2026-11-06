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

"""Exercise opkperformance -> FrameResults -> opkcomm -> Python SDK."""

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
from open_perception_kit.fb.open_perception_kit.metadata.PerformanceMeasurements import PerformanceMeasurementsT


class PerformancePipelineTests(unittest.TestCase):
    def run_pipeline(self, enabled=True):
        plugins = str(Path(sys.argv[1]).resolve())
        environment = os.environ.copy()
        environment["GST_PLUGIN_PATH"] = plugins
        environment["LD_LIBRARY_PATH"] = os.pathsep.join(filter(None, [plugins, environment.get("LD_LIBRARY_PATH")]))
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "frames.ndjson"
            process = run_file_pipeline([
                "videotestsrc", "is-live=true", "num-buffers=6", "!",
                "video/x-raw,format=BGRA,width=64,height=64,framerate=10/1", "!",
                "opkperformance", "name=metrics", "update-interval=3", f"enabled={'true' if enabled else 'false'}", "!",
                "opkcomm", "method=file", f"file-name={output}", "!", "appsink", "wait-on-eos=true", "sync=false", "async=false",
            ], output, 6, env=environment, cwd=Path(__file__).resolve().parents[2], timeout=30)
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
            return [json.loads(line) for line in output.read_text().splitlines() if line]

    def test_publishes_typed_measurements_and_preserves_update_interval(self):
        records = self.run_pipeline()
        self.assertEqual(len(records), 6)
        values = []
        for record in records:
            envelope = decode(base64.b64decode(record["frame_results_packet_b64"], validate=True))
            self.assertTrue(envelope.valid())
            measurements = list(envelope.for_each(PerformanceMeasurementsT))
            self.assertEqual(len(measurements), 1)
            self.assertIn(measurements[0].pixelFormat, ("BGRA", b"BGRA"))
            self.assertIsNotNone(measurements[0].frameRate)
            producer_id = measurements[0].producer.instanceId
            self.assertTrue((producer_id.decode() if isinstance(
                producer_id, bytes) else producer_id).endswith(":metrics"))
            self.assertIn(measurements[0].producer.component, ("opkperformance", b"opkperformance"))
            values.append(measurements[0].frameRate.fps)
        self.assertEqual(values[:2], [None, None])
        self.assertTrue(math.isfinite(values[2]) and values[2] > 0)
        self.assertEqual(values[2:5], [values[2]] * 3)

    def test_disabled_producer_does_not_add_metadata(self):
        records = self.run_pipeline(enabled=False)
        self.assertEqual(len(records), 6)
        self.assertTrue(all(record["frame_results_packet_b64"] is None for record in records))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
