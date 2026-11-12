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

"""Exercise real source archives, ordered patches and packaged patch evidence."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from dependency_patches import (patch_inventory, prepare_sources, stage_patches,
                                validate_patch_files)  # noqa: E402


def change(before, after, filename="value.txt"):
    return (f"diff --git a/{filename} b/{filename}\n"
            f"--- a/{filename}\n+++ b/{filename}\n@@ -1 +1 @@\n-{before}\n+{after}\n")


class DependencyPatchTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.requirements = self.root / "requirements"
        self.queue = self.requirements / "patches/sample"
        self.queue.mkdir(parents=True)
        source = self.root / "sample-1"
        source.mkdir()
        (source / "value.txt").write_text("original\n")
        archive = self.root / "sample.tar.gz"
        with tarfile.open(archive, "w:gz") as tar:
            tar.add(source, arcname=source.name)
        self.lock = {"version": "1", "sample": {
            "name": "sample", "version": "1", "filename": archive.name,
            "url": archive.as_uri(), "sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
            "patches": [],
        }}
        # Filenames sort opposite to the declared application order.
        self.first = self.add_patch("initialize.patch", "original", "first")
        self.add_patch("finalize.patch", "first", "second")

    def add_patch(self, name, before, after):
        path = self.queue / name
        path.write_text(change(before, after))
        self.lock["sample"]["patches"].append({
            "file": path.relative_to(self.requirements).as_posix(), "reason": f"Change {before} to {after}."
        })
        return path

    def prepare(self):
        return prepare_sources(self.lock, self.root / "work", self.requirements)

    def test_apply_and_package_in_declared_order(self):
        # The build can live inside a repository without git apply silently skipping files.
        subprocess.run(["git", "init", "--quiet", str(self.root)], check=True)
        sources, patches = self.prepare()
        self.assertEqual((sources["sample"] / "value.txt").read_text(), "second\n")
        notices = self.root / "notices"
        stage_patches(patches, notices, self.requirements)
        self.assertEqual(validate_patch_files(notices, patches), (
            "patches/sample/initialize.patch", "patches/sample/finalize.patch"))

    def test_packaging_validation(self):
        patches = patch_inventory(self.lock, self.requirements)
        notices = self.root / "notices"
        stage_patches(patches, notices, self.requirements)
        with self.assertRaises(FileExistsError):
            stage_patches(patches[1:], notices, self.requirements)
        with self.assertRaisesRegex(RuntimeError, "do not match"):
            validate_patch_files(notices, list(reversed(patches)))
        self.first.write_text(change("original", "changed"))
        with self.assertRaisesRegex(RuntimeError, "changed during build"):
            stage_patches(patches, self.root / "stale", self.requirements)
        (notices / patches[0]["file"]).write_text("tampered\n")
        with self.assertRaisesRegex(RuntimeError, "checksum mismatch"):
            validate_patch_files(notices)

    def test_conflict_stops_the_queue(self):
        self.lock["sample"]["patches"].reverse()
        with self.assertRaises(subprocess.CalledProcessError):
            self.prepare()
        self.assertEqual((self.root / "work/sample-1/value.txt").read_text(), "original\n")

    def test_missing_duplicate_and_unsafe_patch_paths(self):
        original = self.lock["sample"]["patches"]
        for name in ("patches/sample/missing.patch", "../outside.patch", "/tmp/outside.patch"):
            with self.subTest(path=name):
                self.lock["sample"]["patches"] = [{"file": name, "reason": "Invalid fixture"}]
                with self.assertRaises(ValueError):
                    patch_inventory(self.lock, self.requirements)
        self.lock["sample"]["patches"] = [original[0], original[0]]
        with self.assertRaisesRegex(ValueError, "duplicate"):
            patch_inventory(self.lock, self.requirements)
        outside = self.root / "outside.patch"
        outside.write_text(change("original", "outside"))
        self.first.unlink()
        self.first.symlink_to(outside)
        self.lock["sample"]["patches"] = [original[0]]
        with self.assertRaisesRegex(ValueError, "unsafe"):
            patch_inventory(self.lock, self.requirements)

    def test_patch_cannot_write_outside_source(self):
        self.first.write_text(change("safe", "escaped", "../outside.txt"))
        work = self.root / "work"
        work.mkdir()
        outside = work / "outside.txt"
        outside.write_text("safe\n")
        with self.assertRaises(subprocess.CalledProcessError):
            prepare_sources(self.lock, work, self.requirements)
        self.assertEqual(outside.read_text(), "safe\n")

    def test_check_command_is_independent_of_the_callers_repository(self):
        subprocess.run(["git", "init", "--quiet", str(self.root)], check=True)
        descriptor = self.requirements / "sample.json"
        descriptor.write_text(json.dumps(self.lock))
        tool = Path(__file__).resolve().parents[1] / "dependency_patches.py"
        result = subprocess.run([sys.executable, str(tool), str(descriptor)],
                                env={**os.environ, "GIT_DIR": str(self.root / ".git"),
                                     "GIT_WORK_TREE": str(self.root)},
                                capture_output=True, text=True, check=True)
        self.assertIn(f"OK: {descriptor}", result.stdout)
        self.assertEqual((self.root / "sample-1/value.txt").read_text(), "original\n")

    def test_unpatched_source(self):
        del self.lock["sample"]["patches"]
        sources, patches = self.prepare()
        self.assertEqual(patches, [])
        self.assertEqual((sources["sample"] / "value.txt").read_text(), "original\n")

    def test_source_checksum_is_still_enforced(self):
        self.lock["sample"]["sha256"] = "0" * 64
        with self.assertRaisesRegex(RuntimeError, "checksum mismatch"):
            self.prepare()

    def test_archive_and_source_names_cannot_escape_work_directory(self):
        record = self.lock["sample"]
        for field in ("filename", "name", "version"):
            with self.subTest(field=field):
                original = record[field]
                record[field] = "../../outside"
                with self.assertRaisesRegex(ValueError, "plain filenames"):
                    prepare_sources(self.lock, self.root / "build/work", self.requirements)
                record[field] = original

    def test_patch_edits_cannot_change_applied_bytes(self):
        run_git = subprocess.run

        def edit_patches(*args, **kwargs):
            # Edit the first patch after verification and the second before verification.
            self.first.write_text(change("original", "unrecorded"))
            (self.queue / "finalize.patch").write_text(change("first", "unrecorded"))
            return run_git(*args, **kwargs)

        with patch("subprocess.run", side_effect=edit_patches):
            with self.assertRaisesRegex(RuntimeError, "changed during build"):
                self.prepare()
        self.assertEqual((self.root / "work/sample-1/value.txt").read_text(), "first\n")


if __name__ == "__main__":
    unittest.main()
