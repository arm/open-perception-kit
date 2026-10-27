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
import shutil
import subprocess
import tempfile
import unittest


PACKAGE_SCRIPT = (
    Path(__file__).resolve().parents[3]
    / "scripts/private/executorch/package-executorch-1.3.1-deb.sh"
)


class PackageExecuTorchDebTests(unittest.TestCase):
    def test_package_preserves_required_legal_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sdk = root / "executorch"
            legal = root / "licenses"
            output = root / "packages"
            for header in ("extension/module/module.h", "extension/tensor/tensor_ptr_maker.h", "runtime/core/error.h"):
                path = sdk / "include/executorch" / header
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            (root / "libtorch/include").mkdir(parents=True)
            (sdk / "lib").mkdir()
            source = root / "fixture.c"
            source.write_text("void fixture(void) {}\n")
            subprocess.run(["cc", "-c", str(source), "-o", str(root / "fixture.o")], check=True)
            archive = sdk / "lib/libexecutorch.a"
            subprocess.run(["ar", "rcs", str(archive), str(root / "fixture.o")], check=True)
            for name in (
                "extension_module", "extension_tensor", "extension_flat_tensor", "extension_data_loader",
                "extension_named_data_map", "extension_threadpool", "executorch_core", "pthreadpool",
                "cpuinfo", "portable_ops_lib", "portable_kernels", "XNNPACK", "xnnpack_backend",
                "xnnpack-microkernels-prod", "kleidiai",
            ):
                shutil.copyfile(archive, sdk / f"lib/lib{name}.a")
            legal.mkdir()
            metadata = {
                "LICENSE": b"Original terms\r\n",
                "GIT_COMMIT_ID": b"a" * 40 + b"\n",
                "VERSION_NUMBER": b"1.3.1\n",
            }
            for name, content in metadata.items():
                (legal / name).write_bytes(content)
            command = [
                str(PACKAGE_SCRIPT),
                "--executorch-dir", str(sdk),
                "--libtorch-dir", str(root / "libtorch"),
                "--legal-documentation-dir", str(legal),
                "--output-dir", str(output),
            ]

            for name, content in metadata.items():
                for invalid in (None, b""):
                    with self.subTest(name=name, content=invalid):
                        if invalid is None:
                            (legal / name).unlink()
                        else:
                            (legal / name).write_bytes(invalid)
                        result = subprocess.run(command, capture_output=True, text=True)
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn(name, result.stderr)
                        self.assertFalse(list(output.glob("*.deb")))
                (legal / name).write_bytes(content)

            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            package, = output.glob("*.deb")
            extracted = root / "extracted"
            subprocess.run(["dpkg-deb", "--extract", str(package), str(extracted)], check=True)
            for name, content in metadata.items():
                self.assertEqual(
                    (extracted / "opt/opk-deps/executorch-legal-documentation" / name).read_bytes(), content
                )


if __name__ == "__main__":
    unittest.main()
