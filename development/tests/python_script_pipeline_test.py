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

from open_perception_kit import ProducerIdentityStatus
from open_perception_kit.packet import decode, external_key


class PythonScriptPipelineTest(unittest.TestCase):
    def test_terminal_python_script_publishes_external_payload(self) -> None:
        repository = Path(sys.argv[1]).resolve()
        plugin_directory = Path(sys.argv[2]).resolve()
        model = repository / "config/models/yolo26n-320/yolo26n_raspberry_onnx_optimized.onnx"
        if not model.is_file():
            self.skipTest(
                "YOLO26n-320 model artifact is unavailable; run "
                "scripts/download-models.py first"
            )
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
            temporary_path = Path(temporary_directory)
            output = temporary_path / "frame-results.ndjson"
            script = temporary_path / "terminal_postprocess.py"
            script.write_text(
                "from open_perception_kit.guest import Envelope, external_key\n"
                "from opk_python_ops import Context, Tensor, python_script\n"
                "KEY = external_key('com.arm.example.terminal-test.v1')\n"
                "@python_script\n"
                "def process(env: Envelope, tensors: tuple[Tensor, ...], "
                "context: Context) -> None:\n"
                "    del context\n"
                "    if not tensors:\n"
                "        raise RuntimeError('expected inference output')\n"
                "    env.add(KEY, b'{\"status\":\"ok\"}')\n",
                encoding="utf-8",
            )
            opchain = temporary_path / "opchain-terminal.json"
            opchain.write_text(
                json.dumps(
                    {
                        "version": "1.0.0",
                        "name": "Terminal PythonScript test",
                        "displayName": "Terminal PythonScript test",
                        "task": "Test",
                        "runtime": "ONNX + Python",
                        "description": "Test terminal PythonScript FrameResults output.",
                        "ops": [
                            {
                                "id": "opk-std-ops/InferenceController",
                                "attributes": {},
                            },
                            {
                                "id": "opk-std-ops/GenericImagePreprocess",
                                "attributes": {
                                    "inputImageTensorIndex": 0,
                                    "inputImageSourceName": "pipelineVideoFrame",
                                },
                            },
                            {
                                "id": "opk-onnx-ops/Inference",
                                "attributes": {
                                    "modelDescriptor": str(
                                        repository / "config/models/yolo26n-320/model.json"
                                    )
                                },
                            },
                            {
                                "id": "opk-python-ops/PythonScript",
                                "instanceId": "terminal-python-test",
                                "attributes": {"script": str(script)},
                            },
                        ],
                    }
                ),
                encoding="utf-8",
            )
            command = [
                "gst-launch-1.0",
                "-q",
                "videotestsrc",
                "num-buffers=1",
                "pattern=ball",
                "!",
                "imagefreeze",
                "num-buffers=3",
                "is-live=true",
                "!",
                "videoconvert",
                "!",
                "videoscale",
                "!",
                "video/x-raw,format=BGRA,width=320,height=240,framerate=5/1",
                "!",
                "opkinfer",
                f"opchain-path={opchain}",
                "active=true",
                "!",
                "opkcomm",
                "method=file",
                f"file-name={output}",
                "!",
                "fakesink",
                "async=false",
                "sync=false",
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
            self.assertGreaterEqual(len(records), 1)
            key = external_key("com.arm.example.terminal-test.v1")
            for record in records:
                packet = base64.b64decode(
                    record["frame_results_packet_b64"],
                    validate=True,
                )
                envelope = decode(packet)
                self.assertTrue(envelope.valid(), envelope.error())
                self.assertIs(
                    envelope.producer_identity(),
                    ProducerIdentityStatus.EXACT_MATCH,
                )
                self.assertEqual(list(envelope.for_each(key)), [b'{"status":"ok"}'])


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
