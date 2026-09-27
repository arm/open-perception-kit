#!/usr/bin/env python3
################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Exercise opkcomm file and FIFO opening through a real pipeline."""

import json
import os
from pathlib import Path
import select
import sys
import tempfile
from time import monotonic, sleep
import unittest

from pipeline_test_utils import load_gstreamer_plugins, release_pipeline


class OpkCommFileBoundaryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.plugin = Path(sys.argv[1]).resolve()
        cls.gst = load_gstreamer_plugins([cls.plugin])
        loaded = Path(cls.gst.ElementFactory.find("opkcomm").get_plugin().get_filename()).resolve()
        assert loaded == cls.plugin, f"Wrong opkcomm plugin loaded: {loaded}"

    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)

    def start(self, output, *, succeeds=True):
        pipeline = self.gst.parse_launch(
            "videotestsrc is-live=true ! "
            "video/x-raw,format=BGRA,width=32,height=24,framerate=20/1 ! "
            "opkcomm name=comm method=file ! fakesink sync=false"
        )
        pipeline.get_by_name("comm").set_property("file-name", str(output))
        initial = pipeline.set_state(self.gst.State.PLAYING)
        result, state, _ = pipeline.get_state(3 * self.gst.SECOND)
        if succeeds:
            self.assertNotEqual(initial, self.gst.StateChangeReturn.FAILURE)
            self.assertEqual(state, self.gst.State.PLAYING)
        else:
            self.assertTrue(
                initial == self.gst.StateChangeReturn.FAILURE
                or result == self.gst.StateChangeReturn.FAILURE
            )
        return pipeline

    def records(self, path, count, *, skip=0):
        deadline = monotonic() + 3
        while len(path.read_text().splitlines()) < count + skip:
            self.assertLess(monotonic(), deadline, f"Timed out waiting for {count} records")
            sleep(0.01)
        lines = path.read_text().splitlines()
        return [json.loads(line) for line in lines[skip: skip + count]]

    def fifo_record(self, fd):
        data = b""
        deadline = monotonic() + 3
        while b"\n" not in data:
            remaining = deadline - monotonic()
            self.assertGreater(remaining, 0, "Timed out waiting for FIFO record")
            if select.select([fd], [], [], remaining)[0]:
                data += os.read(fd, 65536)
        return json.loads(data.splitlines()[0])

    def test_file_symlink_fifo_and_recovery(self):
        baseline = self.root / "baseline.ndjson"
        baseline.write_text("BLUE_SENTINEL\n")
        pipeline = self.start(baseline)
        try:
            records = self.records(baseline, 2, skip=1)
            self.assertEqual(baseline.read_text().splitlines()[0], "BLUE_SENTINEL")
            self.assertLess(records[0]["frame_counter"], records[1]["frame_counter"])
        finally:
            release_pipeline(pipeline, self.gst)

        victim = self.root / "victim.ndjson"
        victim.write_text("BLUE_SENTINEL\n")
        link = self.root / "output.ndjson"
        link.symlink_to(victim)
        pipeline = self.start(link, succeeds=False)
        release_pipeline(pipeline, self.gst)
        self.assertTrue(link.is_symlink())
        self.assertEqual(victim.read_text(), "BLUE_SENTINEL\n")

        fifo = self.root / "records.fifo"
        os.mkfifo(fifo, 0o600)
        pipeline = self.start(fifo, succeeds=False)
        release_pipeline(pipeline, self.gst)

        first_reader = os.open(fifo, os.O_RDONLY | os.O_NONBLOCK)
        pipeline = self.start(fifo)
        try:
            first = self.fifo_record(first_reader)
            os.close(first_reader)
            first_reader = -1
            sleep(0.2)  # Four 20 Hz frames give the writer time to observe EPIPE.
            second_reader = os.open(fifo, os.O_RDONLY | os.O_NONBLOCK)
            try:
                second = self.fifo_record(second_reader)
                self.assertLess(first["frame_counter"], second["frame_counter"])
            finally:
                os.close(second_reader)
        finally:
            if first_reader >= 0:
                os.close(first_reader)
            release_pipeline(pipeline, self.gst)

        recovery = self.root / "recovery.ndjson"
        pipeline = self.start(recovery)
        try:
            records = self.records(recovery, 2)
            self.assertLess(records[0]["frame_counter"], records[1]["frame_counter"])
        finally:
            release_pipeline(pipeline, self.gst)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]], verbosity=2)
