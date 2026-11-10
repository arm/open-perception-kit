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

import json
import subprocess
import unittest
from pathlib import Path

from scripts.testing.nightly_image_pipeline import image_pipeline

SCRIPT = Path(__file__).with_name("nightly-package-smoke.sh")


class PackageSmokeTest(unittest.TestCase):
    def test_rejects_unsupported_package_kind_before_installing(self):
        result = subprocess.run(["bash", str(SCRIPT), "unknown"], capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn(b"Usage:", result.stderr)

    def test_packaged_preset_uses_its_image_source_and_inference(self):
        repo = Path(__file__).resolve().parents[2]
        preset = json.loads((repo / "config/pipelines/yolo26n-320.json").read_text())
        result = image_pipeline(preset)
        self.assertEqual(result["pipeline"][:4], preset["alternative-source-image"])
        self.assertTrue(result["pipeline"][4].startswith("opkinfer "))
        self.assertEqual(result["pipeline"][4:], preset["pipeline"][4:])
        self.assertNotIn("data/videos", " ".join(result["pipeline"]))
        self.assertFalse(result["loop"])

    def test_rejects_missing_or_ambiguous_inference(self):
        for stages in ([], ["opkinfer !", "opkinfer !"]):
            with self.subTest(stages=stages):
                with self.assertRaises(ValueError):
                    image_pipeline({"pipeline": stages, "alternative-source-image": ["filesrc !"]})

        with self.assertRaises(ValueError):
            image_pipeline({"pipeline": ["opkinfer !"], "alternative-source-image": []})


if __name__ == "__main__":
    unittest.main()
