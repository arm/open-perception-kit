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

from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SETUP_SCRIPT = REPOSITORY_ROOT / "scripts/setup-python-ops-runtime.sh"


class PythonRuntimeSetupTests(unittest.TestCase):
    def test_check_reports_versions_without_creating_a_venv(self) -> None:
        import flatbuffers
        import numpy

        with tempfile.TemporaryDirectory() as temporary:
            venv = Path(temporary) / "unused"
            result = subprocess.run(
                ["bash", str(SETUP_SCRIPT), "--check", "--python", sys.executable,
                 "--venv", str(venv)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.splitlines(), [
                f"{sys.version_info.major}.{sys.version_info.minor}",
                numpy.__version__, flatbuffers.__version__, numpy.get_include(),
            ])
            self.assertFalse(venv.exists())

    def test_check_rejects_each_mismatched_version(self) -> None:
        for name in ("python", "numpy", "flatbuffers"):
            with self.subTest(dependency=name), tempfile.TemporaryDirectory() as temporary:
                runtime = json.loads(
                    (REPOSITORY_ROOT / "development/ops-python/runtime.json").read_text())
                sdk = json.loads((REPOSITORY_ROOT / "tools/perception/sdk.json").read_text())
                descriptor = sdk if name == "flatbuffers" else runtime
                descriptor[name]["version"] = "0.0.0"
                runtime_path = Path(temporary) / "runtime.json"
                sdk_path = Path(temporary) / "sdk.json"
                runtime_path.write_text(json.dumps(runtime))
                sdk_path.write_text(json.dumps(sdk))
                result = subprocess.run(
                    ["bash", str(SETUP_SCRIPT), "--check", "--python", sys.executable,
                     "--runtime-json", str(runtime_path), "--sdk-json", str(sdk_path)],
                    capture_output=True, text=True, timeout=30)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(f"requires {name if name != 'python' else 'Python'} 0.0.0",
                              result.stderr)


class PythonRuntimeAttachmentTests(unittest.TestCase):
    def test_rejects_mismatched_dependency_versions(self) -> None:
        for name in ("numpy", "flatbuffers"):
            with self.subTest(module=name):
                result = subprocess.run(
                    [sys.executable, "-c", """
import importlib
import sys

dependency = importlib.import_module(sys.argv[1])
dependency.__version__ = "0.0.0"
import opk_python_runtime_attachment_test
opk_python_runtime_attachment_test.run()
""", name],
                    cwd=Path(sys.argv[1]).resolve().parent,
                    capture_output=True,
                    text=True,
                    timeout=30,
                )
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn(
                    f"Python module {name} has version 0.0.0; expected", result.stderr)

    def test_repeated_operators_attach_to_host_interpreter(self) -> None:
        module_path = Path(sys.argv[1])
        spec = importlib.util.spec_from_file_location(
            "opk_python_runtime_attachment_test", module_path
        )
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        module.run()
        module.run()

        self.assertIn("open_perception_kit_bridge", sys.modules)
        self.assertIn("opk_python_ops", sys.modules)
        self.assertTrue(
            hasattr(sys.modules["open_perception_kit_bridge"], "Envelope"))
        self.assertTrue(hasattr(sys.modules["opk_python_ops"], "Tensor"))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
