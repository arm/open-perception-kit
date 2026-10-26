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

from pathlib import Path
import tempfile
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "examples/mlwl-gpu"))
import prepare as runtime  # noqa: E402
import rpi5  # noqa: E402

GPU = "vendorID = 0x14e4\ndeviceType = PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU\ndeviceName = V3D 7.1.10.2\n"


class Rpi5ExampleTest(unittest.TestCase):
    def test_native_device(self):
        rpi5.check_gpu(GPU)
        for invalid in ("", GPU * 2, GPU.replace("INTEGRATED_GPU", "CPU"),
                        GPU.replace("0x14e4", "0x13b5"), GPU.replace("V3D 7.1", "V3D 4.2")):
            with self.subTest(summary=invalid), self.assertRaises(RuntimeError):
                rpi5.check_gpu(invalid)

    def test_wrong_architecture_cannot_install(self):
        with patch.object(rpi5.platform, "machine", return_value="x86_64"), patch.object(rpi5, "run") as run:
            with self.assertRaisesRegex(RuntimeError, "AArch64"):
                rpi5.main()
            run.assert_not_called()

    def test_patch_retry_and_source_drift(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "source.c"
            source.write_text("old\n")
            runtime.patch(source, "old", "new")
            runtime.patch(source, "old", "new")
            self.assertEqual(source.read_text(), "new\n")
            source.write_text("old old\n")
            with self.assertRaises(RuntimeError):
                runtime.patch(source, "old", "new")
            self.assertEqual(source.read_text(), "old old\n")

    def test_completed_runtime_skips_preparation(self):
        with tempfile.TemporaryDirectory() as temporary, patch.object(runtime, "run") as run:
            cache = Path(temporary)
            (cache / "ready").touch()
            runtime.prepare(cache, cache / "sdk")
            run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
