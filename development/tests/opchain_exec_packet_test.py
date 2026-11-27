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

"""Check binary packet export and I/O errors with a model-free test OpChain."""

import errno
import os
from pathlib import Path
import resource
import signal
import subprocess
import sys
import tempfile
import unittest

from open_perception_kit.packet import decode
from pipeline_test_utils import write_test_opchain


class OpChainExecPacketTest(unittest.TestCase):
    def test_packet_export(self):
        executable, plugin, image = map(Path, sys.argv[1:])
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            chain = write_test_opchain(temporary, "export", {})
            output = directory / "result.frame"
            command = [executable, chain, image, "--packet-out"]
            environment = os.environ.copy()
            library_paths = [str(plugin.parent), environment.get("LD_LIBRARY_PATH", "")]
            environment["LD_LIBRARY_PATH"] = os.pathsep.join(filter(None, library_paths))

            def run_export(path, **options):
                return subprocess.run([*command, path], env=environment,
                                      capture_output=True, text=True, **options)

            result = run_export(output)
            self.assertEqual(result.returncode, 0, result.stderr)
            packet = output.read_bytes()
            envelope = decode(packet)
            self.assertTrue(envelope.valid(), envelope.error())
            self.assertIn(f"Perception packet bytes: {len(packet)}, payloads={envelope.size()}",
                          result.stdout)

            def limit_output_size():
                signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
                limit = len(packet) - 1
                resource.setrlimit(resource.RLIMIT_FSIZE, (limit, limit))

            for path, options, error in (
                (directory / "missing" / "result.frame", {}, errno.ENOENT),
                (output, {"preexec_fn": limit_output_size}, errno.EFBIG),
            ):
                with self.subTest(error=error):
                    result = run_export(path, **options)
                    self.assertEqual(result.returncode, 1, result.stderr)
                    self.assertIn(f"{path}: {os.strerror(error)}", result.stderr)
                    self.assertNotIn("Perception packet bytes:", result.stdout)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
