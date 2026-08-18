#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import hashlib
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DOWNLOAD_SCRIPT = REPO_ROOT / "scripts/private/download-demo-videos.sh"


class DemoVideoDownloadTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        self.private_scripts = self.root / "scripts/private"
        self.private_scripts.mkdir(parents=True)
        shutil.copy2(DOWNLOAD_SCRIPT, self.private_scripts / DOWNLOAD_SCRIPT.name)

        self.filename = "GettyImages-1.mov"
        self.payload = b"verified demo video"
        checksum = hashlib.sha256(self.payload).hexdigest()
        (self.private_scripts / "demo-videos.manifest").write_text(
            f"{checksum} f_1 {self.filename}\n"
        )

        fake_bin = self.root / "bin"
        fake_bin.mkdir()
        curl = fake_bin / "curl"
        curl.write_text(
            "#!/bin/sh\n"
            "while [ \"$#\" -gt 0 ]; do\n"
            "  case \"$1\" in -o) output=$2; shift 2 ;; *) shift ;; esac\n"
            "done\n"
            "printf %s \"$FAKE_CURL_CONTENT\" > \"$output\"\n"
        )
        curl.chmod(0o755)
        self.environment = os.environ | {
            "FAKE_CURL_CONTENT": self.payload.decode(),
            "PATH": f"{fake_bin}:{os.environ['PATH']}",
        }

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def run_downloader(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [str(self.private_scripts / DOWNLOAD_SCRIPT.name), *arguments],
            capture_output=True,
            check=False,
            env=self.environment,
            text=True,
        )

    def test_download_writes_and_checks_checksum_manifest(self) -> None:
        self.assertEqual(self.run_downloader("--check").returncode, 1)

        downloaded = self.run_downloader()
        self.assertEqual(downloaded.returncode, 0, downloaded.stderr)

        video = self.root / "data/videos" / self.filename
        manifest = video.parent / "SHA256SUMS"
        self.assertEqual(video.read_bytes(), self.payload)
        self.assertEqual(
            manifest.read_text(),
            f"{hashlib.sha256(self.payload).hexdigest()}  {self.filename}\n",
        )
        self.assertEqual(self.run_downloader("--check").returncode, 0)

        video.write_bytes(b"corrupt")
        failed = self.run_downloader()
        self.assertEqual(failed.returncode, 0, failed.stderr)
        self.assertEqual(video.read_bytes(), self.payload)

    def test_rejects_download_with_wrong_checksum(self) -> None:
        self.environment["FAKE_CURL_CONTENT"] = "wrong payload"
        failed = self.run_downloader()
        self.assertEqual(failed.returncode, 1)
        self.assertFalse((self.root / "data/videos" / self.filename).exists())


if __name__ == "__main__":
    unittest.main()
