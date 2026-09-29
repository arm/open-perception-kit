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

"""Regressions for false-green image scans and unuploadable diagnostics."""

import importlib
import json
import os
from pathlib import Path
import tarfile
import tempfile
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import MagicMock, patch


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
SCAN = importlib.import_module("blackduck_image_scan")


class ImageScanTests(unittest.TestCase):
    def test_invocation_preserves_filesystem_without_squashing_and_checks_revision(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            args = SimpleNamespace(wrapper=root / "detect.sh", image="test-image", source_sha="a" * 40,
                                   version="feature/test", label="ci-image", output=root / "output")
            metadata = [{"Id": "sha256:" + "b" * 64, "Architecture": "amd64",
                         "Config": {"Labels": {"org.opencontainers.image.revision": args.source_sha}}}]

            def native_detect(command, **_kwargs):
                self.assertIn("--detect.cleanup=false", command)
                self.assertIn("--detect.project.version.name=feature-test", command)
                self.assertEqual(command[:2], ["timeout", "--kill-after=30"])
                self.assertLessEqual(int(command[2]), SCAN.IMAGE_TIMEOUT)
                self.assertGreater(SCAN.IMAGE_TIMEOUT, SCAN.DETECT_TIMEOUT)
                extraction = args.output / "runs/run/extractions/DOCKER-0"
                filesystem = extraction / "image_containerfilesystem.tar.gz"
                lines = ["token=test-secret-value\n", "Process return code: 0\n"]
                if "--detect.tools=DOCKER" in command:
                    self.assertIn("--detect.docker.passthrough.output.include.squashedimage=false", command)
                    self.assertIn("--detect.docker.passthrough.output.include.containerfilesystem=true", command)
                    extraction.mkdir(parents=True)
                    filesystem.write_bytes(b"native filesystem")
                    (extraction / "image_bdio.jsonld").write_text('{"native": "inventory"}')
                else:
                    self.assertIn("--detect.tools=SIGNATURE_SCAN", command)
                    self.assertIn(f"--detect.blackduck.signature.scanner.paths={filesystem}", command)
                    lines.append(f"Black Duck CLI command: java --scan {filesystem}\n")
                process = MagicMock()
                process.__enter__.return_value = process
                process.stdout = [*lines, "Overall Status: SUCCESS\n"]
                process.wait.return_value = 0
                return process

            with patch.dict(os.environ, BLACKDUCK_TOKEN="test-secret-value",
                            BLACKDUCK_URL="https://blackduck.example", GITHUB_REPOSITORY="org/repo"), \
                    patch.object(SCAN.subprocess, "check_output", return_value=json.dumps(metadata)), \
                    patch.object(SCAN.time, "monotonic", side_effect=[0, 0, 30]), \
                    patch.object(SCAN.subprocess, "Popen", side_effect=native_detect) as process:
                SCAN.scan_image(args)
                self.assertEqual(process.call_count, 2)
                self.assertEqual(int(process.call_args_list[1].args[0][2]), SCAN.IMAGE_TIMEOUT - 30)
                self.assertNotIn("test-secret-value", (args.output / "scan.log").read_text())
                self.assertEqual(json.loads((args.output / "verification.json").read_text())["status"], "SUCCESS")
                process.reset_mock()
                args.source_sha = "c" * 40
                with self.assertRaises(ValueError):
                    SCAN.scan_image(args)
                process.assert_not_called()

    def test_real_target_required_even_when_detect_reports_success(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            extraction = root / "runs/run/extractions/DOCKER-0"
            extraction.mkdir(parents=True)
            filesystem = extraction / "image_containerfilesystem.tar.gz"
            filesystem.write_bytes(b"filesystem")
            (extraction / "image_bdio.jsonld").write_text('{"native": "inventory"}')
            success = (f"Process return code: 0\nBlack Duck CLI command: java --scan {filesystem}\n"
                       "Overall Status: SUCCESS\n")
            self.assertEqual(SCAN.verify_image_scan(root, success), filesystem)
            for broken in (
                success.replace("return code: 0", "return code: 255"),
                success.replace(str(filesystem), "/runner/workspace"),
                success + "Error inspecting image: Too many levels of symbolic links\n",
                success.replace("Overall Status: SUCCESS", "Overall Status: FAILURE"),
                success + "Overall Status: FAILURE\n",
                "Overall Status: SUCCESS\n",
            ):
                with self.subTest(log=broken), self.assertRaises(RuntimeError):
                    SCAN.verify_image_scan(root, broken)
            filesystem.unlink()
            with self.assertRaises(RuntimeError):
                SCAN.verify_image_scan(root, success)

    def test_archive_keeps_colon_logs_not_filesystems_or_symlinks(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            log = root / "runs/run/extractions/DOCKER-0/inspector:debug.log"
            log.parent.mkdir(parents=True)
            log.write_text("token=test-secret-value\n")
            filesystem = log.parent / "inspectorSharedDir/containerFileSystem"
            filesystem.mkdir(parents=True)
            (filesystem / "binutils:amd64.log").write_text("not diagnostics")
            (root / "image_containerfilesystem.tar.gz").write_bytes(b"not diagnostics")
            (root / "input").mkdir()
            (root / "input/third-party.log").write_text("scan input, not diagnostics")
            (root / "cycle").symlink_to(root, target_is_directory=True)
            (root / "outside.log").symlink_to("/etc/passwd")
            (root / "status.json").write_text('{"status": "failure"}')
            destination = root / "diagnostics.tar.gz"
            with patch.dict(os.environ, BLACKDUCK_TOKEN="test-secret-value"):
                SCAN.archive_diagnostics(root, destination)
            with tarfile.open(destination) as archive:
                self.assertEqual(set(archive.getnames()), {str(log.relative_to(root)), "status.json"})
                member = archive.extractfile(str(log.relative_to(root)))
                assert member is not None
                self.assertEqual(member.read(), b"token=***\n")


if __name__ == "__main__":
    unittest.main()
