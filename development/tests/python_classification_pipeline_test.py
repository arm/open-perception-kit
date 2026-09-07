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

from perception import ProducerIdentityStatus
from perception.fb.perception.metadata.Classifications import ClassificationsT
from perception.packet import decode, external_key


class PythonScriptPipelineTest(unittest.TestCase):
    def test_pipeline_loads_plugin_and_publishes_both_classifiers(self) -> None:
        repository = Path(sys.argv[1]).resolve()
        plugin_directory = Path(sys.argv[2]).resolve()
        model = (
            repository
            / "config/models/mobilenetv2/mobilenet_v2_1.4_224.onnx"
        )
        if not model.is_file():
            self.skipTest(
                "MobileNet model artifact is unavailable; run "
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
            output = Path(temporary_directory) / "frame-results.ndjson"
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
                "pekinfer",
                f"opchain-path={repository / 'config/models/mobilenetv2/opchain-python-classification.json'}",
                "active=true",
                "!",
                "pekcomm",
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

    def test_terminal_python_script_publishes_external_payload(self) -> None:
        repository = Path(sys.argv[1]).resolve()
        plugin_directory = Path(sys.argv[2]).resolve()
        model = repository / "config/models/mobilenetv2/mobilenet_v2_1.4_224.onnx"
        if not model.is_file():
            self.skipTest(
                "MobileNet model artifact is unavailable; run "
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
                "from perception.guest import Envelope, external_key\n"
                "from pek_python_ops import Context, Tensor, python_script\n"
                "KEY = external_key('com.arm.example.terminal-test.v1')\n"
                "@python_script\n"
                "def process(env: Envelope, tensors: tuple[Tensor, ...], "
                "context: Context) -> None:\n"
                "    del context\n"
                "    if len(tensors) != 1:\n"
                "        raise RuntimeError('expected one inference output')\n"
                "    env.add(KEY, b'{\"status\":\"ok\"}')\n",
                encoding="utf-8",
            )
            opchain = temporary_path / "opchain-terminal.json"
            opchain.write_text(
                json.dumps(
                    {
                        "version": 1,
                        "name": "Terminal PythonScript test",
                        "displayName": "Terminal PythonScript test",
                        "task": "Test",
                        "runtime": "ONNX + Python",
                        "description": "Test terminal PythonScript FrameResults output.",
                        "ops": [
                            {
                                "id": "pek-std-ops/InferenceController",
                                "attributes": {},
                            },
                            {
                                "id": "pek-std-ops/GenericImagePreprocess",
                                "attributes": {
                                    "inputImageTensorIndex": 0,
                                    "inputImageSourceName": "pipelineVideoFrame",
                                },
                            },
                            {
                                "id": "pek-onnx-ops/Inference",
                                "attributes": {
                                    "modelDescriptor": str(
                                        repository / "config/models/mobilenetv2/model.json"
                                    )
                                },
                            },
                            {
                                "id": "pek-python-ops/PythonScript",
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
                "pekinfer",
                f"opchain-path={opchain}",
                "active=true",
                "!",
                "pekcomm",
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
