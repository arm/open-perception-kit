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

"""Check opkcomm file and FIFO opening."""

import ctypes
import os
from pathlib import Path
import sys
import tempfile
import unittest

from pipeline_test_utils import load_gstreamer_plugins, release_pipeline


class OpkCommFileBoundaryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        plugin = Path(sys.argv[1]).resolve()
        cls.gst = load_gstreamer_plugins([plugin])
        loaded = Path(cls.gst.ElementFactory.find("opkcomm").get_plugin().get_filename()).resolve()
        assert loaded == plugin, f"Wrong opkcomm plugin loaded: {loaded}"

    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)

    def open_output(self, path):
        element = self.gst.ElementFactory.make("opkcomm")
        self.addCleanup(release_pipeline, element, self.gst)
        element.set_property("method", "file")
        element.set_property("file-name", str(path))
        return element.set_state(self.gst.State.PAUSED)

    def test_regular_file(self):
        directory = self.root / "records"
        directory.mkdir()
        output = directory / "output.ndjson"
        self.assertEqual(self.open_output(output), self.gst.StateChangeReturn.SUCCESS)
        self.assertEqual(output.read_text(), "")
        output.write_text("SENTINEL\n")
        self.assertEqual(self.open_output(os.path.relpath(output)), self.gst.StateChangeReturn.SUCCESS)
        self.assertEqual(output.read_text(), "SENTINEL\n")

    def test_symlink_rejected(self):
        victim = self.root / "victim.ndjson"
        victim.write_text("SENTINEL\n")
        link = self.root / "output.ndjson"
        link.symlink_to(victim)
        self.assertEqual(self.open_output(link), self.gst.StateChangeReturn.FAILURE)
        self.assertTrue(link.is_symlink())
        self.assertEqual(victim.read_text(), "SENTINEL\n")

    def test_symlinked_directory_rejected(self):
        target = self.root / "target"
        target.mkdir()
        victim = target / "existing.ndjson"
        victim.write_text("SENTINEL\n")
        link = self.root / "linked-directory"
        link.symlink_to(target, target_is_directory=True)
        for name in ("existing.ndjson", "new.ndjson"):
            for path in (link / name, os.path.relpath(link / name)):
                with self.subTest(path=path):
                    self.assertEqual(self.open_output(path), self.gst.StateChangeReturn.FAILURE)
        self.assertEqual(victim.read_text(), "SENTINEL\n")
        self.assertFalse((target / "new.ndjson").exists())

    def test_fifo(self):
        fifo = self.root / "records.fifo"
        os.mkfifo(fifo, 0o600)
        self.assertEqual(self.open_output(fifo), self.gst.StateChangeReturn.FAILURE)
        with os.fdopen(os.open(fifo, os.O_RDONLY | os.O_NONBLOCK), "rb"):
            self.assertEqual(self.open_output(fifo), self.gst.StateChangeReturn.SUCCESS)

    def test_device_rejected_without_opening(self):
        master, slave = os.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        device = os.ttyname(slave)
        libc = ctypes.CDLL(None, use_errno=True)
        events = libc.inotify_init1(os.O_NONBLOCK | os.O_CLOEXEC)
        self.assertGreaterEqual(events, 0)
        self.addCleanup(os.close, events)
        self.assertGreaterEqual(libc.inotify_add_watch(events, os.fsencode(device), 0x20), 0)  # IN_OPEN
        self.assertEqual(self.open_output(device), self.gst.StateChangeReturn.FAILURE)
        with self.assertRaises(BlockingIOError):
            os.read(events, 4096)

    @unittest.skipUnless(os.geteuid() == 0, "requires chown to another user")
    def test_shared_fifo(self):
        if Path("/proc/sys/fs/protected_fifos").read_text().strip() == "0":
            self.skipTest("requires fs.protected_fifos")
        self.root.chmod(0o1777)
        fifo = self.root / "shared.fifo"
        os.mkfifo(fifo, 0o666)
        os.chown(fifo, 65534, -1)
        with os.fdopen(os.open(fifo, os.O_RDONLY | os.O_NONBLOCK), "rb"):
            self.assertEqual(self.open_output(fifo), self.gst.StateChangeReturn.SUCCESS)

    @unittest.skipUnless(os.geteuid() == 0, "requires chown to another user")
    def test_shared_regular_file_stays_protected(self):
        protection = int(Path("/proc/sys/fs/protected_regular").read_text())
        if protection == 0:
            self.skipTest("requires fs.protected_regular")
        overflow_uid = int(Path("/proc/sys/kernel/overflowuid").read_text())
        owner = 65533 if overflow_uid != 65533 else 65534
        self.root.chmod(0o1777)
        output = self.root / "shared.ndjson"
        output.write_text("SENTINEL\n")
        os.chown(output, owner, -1)
        self.assertEqual(self.open_output(output), self.gst.StateChangeReturn.FAILURE)
        self.assertEqual(output.read_text(), "SENTINEL\n")
        self.root.chmod(0o1770)
        expected = self.gst.StateChangeReturn.FAILURE if protection > 1 else self.gst.StateChangeReturn.SUCCESS
        self.assertEqual(self.open_output(output), expected)
        os.chown(self.root, owner, -1)
        self.assertEqual(self.open_output(output), self.gst.StateChangeReturn.SUCCESS)
        self.root.chmod(0o1777)
        os.chown(self.root, overflow_uid, -1)
        os.chown(output, overflow_uid, -1)
        self.assertEqual(self.open_output(output), self.gst.StateChangeReturn.FAILURE)
        self.root.chmod(0o777)
        os.chown(self.root, 0, -1)
        self.assertEqual(self.open_output(output), self.gst.StateChangeReturn.SUCCESS)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]], verbosity=2)
