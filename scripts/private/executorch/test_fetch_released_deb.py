# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("fetch-released-deb.sh")


class FetchReleasedDebTest(unittest.TestCase):
    def test_download_and_checksum_for_each_architecture(self):
        pins = {
            "amd64": "ec015e7dab028e1803ae38f7fed21e42fd392a2eddce3c6a6d016ed6170b4ddd",
            "arm64": "84d2c40bae3a1b27e9e43d1ed54be257486305d2432454e4597441e970339d97",
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "curl").write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$TEST_CURL_ARGS"\n')
            (root / "sha256sum").write_text('#!/bin/sh\ncat > "$TEST_CHECKSUM"\n')
            for name in ("curl", "sha256sum"):
                (root / name).chmod(0o755)
            env = dict(os.environ, PATH=f"{root}:{os.environ['PATH']}")
            env.update(TEST_CURL_ARGS=str(root / "curl-args"), TEST_CHECKSUM=str(root / "checksum"))
            for arch, digest in pins.items():
                with self.subTest(arch=arch):
                    subprocess.run(["bash", str(SCRIPT), arch, str(root / "out")], env=env, check=True)
                    filename = f"libexecutorch-dev-1.3.1-3-{arch}.deb"
                    self.assertIn(
                        f"https://artifacts.tools.arm.com/open-perception-kit/executorch/{filename}",
                        (root / "curl-args").read_text(),
                    )
                    self.assertEqual(
                        (root / "checksum").read_text(), f"{digest}  {root / 'out' / filename}\n"
                    )

    def test_rejects_unsupported_architecture(self):
        result = subprocess.run(["bash", str(SCRIPT), "unknown", "/tmp/unused"], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"Unsupported architecture", result.stderr)

    def test_checksum_failure_fails_fetch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "curl").write_text("#!/bin/sh\nexit 0\n")
            (root / "sha256sum").write_text("#!/bin/sh\nexit 1\n")
            for name in ("curl", "sha256sum"):
                (root / name).chmod(0o755)
            env = dict(os.environ, PATH=f"{root}:{os.environ['PATH']}")
            result = subprocess.run(["bash", str(SCRIPT), "amd64", str(root / "out")], env=env)
            self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
