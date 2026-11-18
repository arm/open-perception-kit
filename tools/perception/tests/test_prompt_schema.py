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

"""Prompt schema round trips in the generated SDKs (run in the OPK container)."""

from __future__ import annotations

import importlib
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parents[3]
SDK = REPO / "generated/open_perception_kit"
FIXTURES = Path(__file__).with_name("fixtures")
sys.path.insert(0, str(SDK / "python/src"))
sys.path.insert(0, str(REPO / "tools/perception"))
Envelope = importlib.import_module("open_perception_kit.sdk").Envelope
prepare_rust_vendor = importlib.import_module("package").prepare_rust_vendor
load_sdk_config = importlib.import_module("sdk_config").load_sdk_config


def native(name: str):
    module = importlib.import_module(
        f"open_perception_kit.fb.open_perception_kit.metadata.{name}"
    )
    return getattr(module, f"{name}T")


class PromptSchemaTests(unittest.TestCase):
    def test_python_optional_payload_and_unsigned_ids(self) -> None:
        request_id = 0xFEDCBA9876543210
        revision = 0xFFFFFFFFFFFFFFFE
        request = native("RequestEnvelope")(
            requestId=request_id,
            revision=revision,
            destination=native("RequestDestination")(
                inferenceInstance="sam", sourceName="pipelineVideoFrame"
            ),
        )
        prompted = native("BoxPromptInputs")(
            request=request,
            payload=native("BoxPromptPayload")(
                box=native("PromptBox")(left=0.0, top=0.25, right=1.0, bottom=0.75)
            ),
        )
        association = native("PromptAssociation")(
            request=request, sessionId="session-a", parentId=0x8000000000000001
        )
        envelope = Envelope()
        envelope.add(prompted)
        envelope.add(native("BoxPromptInputs")())
        envelope.add(native("BoxPromptInputs")(request=request))
        envelope.add(association)
        result = Envelope(envelope.serialize())
        decoded = result.get(native("BoxPromptInputs"))
        self.assertEqual(decoded.request.requestId, request_id)
        self.assertEqual(decoded.request.revision, revision)
        self.assertEqual(decoded.payload.box.top, 0.25)
        self.assertEqual(decoded.payload.box.right, 1.0)
        self.assertIsNone(result.get(native("BoxPromptInputs"), 1).request)
        self.assertIsNone(result.get(native("BoxPromptInputs"), 1).payload)
        self.assertEqual(result.get(native("BoxPromptInputs"), 2).request.requestId, request_id)
        self.assertIsNone(result.get(native("BoxPromptInputs"), 2).payload)
        decoded_association = result.get(native("PromptAssociation"))
        self.assertEqual(decoded_association.request.requestId, request_id)
        self.assertEqual(decoded_association.parentId, 0x8000000000000001)
        self.assertIn(decoded_association.sessionId, ("session-a", b"session-a"))

    def test_cpp_optional_payload_and_unsigned_ids(self) -> None:
        with tempfile.TemporaryDirectory(prefix="opk-prompt-cpp-") as temp:
            binary = Path(temp) / "prompt-schema-test"
            subprocess.run([
                "c++", "-std=c++20", "-pthread", "-I", str(SDK / "cpp"),
                str(FIXTURES / "prompt_schema.cpp"), "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True)

    def test_rust_optional_payload_and_unsigned_ids(self) -> None:
        with tempfile.TemporaryDirectory(prefix="opk-prompt-rust-") as temp:
            project = Path(temp) / "sdk"
            shutil.copytree(SDK / "rust", project)
            shutil.copyfile(FIXTURES / "prompt_schema.rs", project / "tests/prompt_schema.rs")
            artifact_dir = os.environ.get("OPK_SDK_ARTIFACT_DIR")
            prepare_rust_vendor(
                rust_root=project,
                workspace=Path(temp),
                config=load_sdk_config(),
                artifact_dir=Path(artifact_dir) if artifact_dir else None,
            )
            subprocess.run([
                "cargo", "test", "--offline", "--manifest-path", str(project / "Cargo.toml"),
                "--test", "prompt_schema",
            ], cwd=project, check=True)

    def test_typescript_optional_payload_and_unsigned_ids(self) -> None:
        with tempfile.TemporaryDirectory(prefix="opk-prompt-ts-") as temp:
            project = Path(temp) / "sdk"
            shutil.copytree(SDK / "ts", project)
            modules = project / "node_modules"
            modules.mkdir()
            global_modules = Path(subprocess.check_output(
                ["npm", "root", "--global"], text=True
            ).strip())
            (modules / "flatbuffers").symlink_to(global_modules / "flatbuffers", target_is_directory=True)
            script = project / "prompt_schema.mjs"
            shutil.copyfile(FIXTURES / "prompt_schema.mjs", script)
            subprocess.run(["node", str(script)], check=True)


if __name__ == "__main__":
    unittest.main()
