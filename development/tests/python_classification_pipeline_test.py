################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

import base64
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from perception.fb.perception.metadata.Classifications import ClassificationsT
from perception.packet import decode


class PythonScriptPipelineTest(unittest.TestCase):
    def test_pipeline_loads_plugin_and_publishes_both_classifiers(self) -> None:
        repository = Path(sys.argv[1]).resolve()
        plugin_directory = Path(sys.argv[2]).resolve()
        environment = os.environ.copy()
        environment["GST_PLUGIN_PATH"] = str(plugin_directory)
        environment["LD_LIBRARY_PATH"] = os.pathsep.join(
            filter(
                None,
                [
                    str(plugin_directory),
                    environment.get("LD_LIBRARY_PATH", ""),
                ],
            )
        )

        with tempfile.TemporaryDirectory() as temporary_directory:
            output = Path(temporary_directory) / "frame-results.ndjson"
            command = [
                "gst-launch-1.0",
                "-q",
                "videotestsrc",
                "num-buffers=3",
                "pattern=ball",
                "!",
                "videoconvert",
                "!",
                "videoscale",
                "!",
                "video/x-raw,format=BGRA,width=320,height=240,framerate=5/1",
                "!",
                "pekinfer",
                f"opchain-path={repository / 'config/models/mobilenetv2/opchain-python-classification.json'}",
                "active=true",
                "!",
                "pekcomm",
                "method=file",
                f"file-name={output}",
                "!",
                "fakesink",
            ]
            subprocess.run(
                command,
                cwd=repository,
                env=environment,
                check=True,
                timeout=60,
            )

            records = [
                json.loads(line)
                for line in output.read_text(encoding="utf-8").splitlines()
                if line
            ]
            self.assertEqual(len(records), 3)
            for record in records:
                packet = base64.b64decode(record["frame_results_packet_b64"], validate=True)
                envelope = decode(packet)
                payloads = list(envelope.for_each(ClassificationsT))
                self.assertEqual(len(payloads), 2)
                implementations = {
                    payload.layer.producer.implementation.decode("utf-8")
                    for payload in payloads
                }
                self.assertEqual(
                    implementations,
                    {"ImageNetClassificationParser", "python_classification.py"},
                )


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
