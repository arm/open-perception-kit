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

"""Consume the native semantic parser's class map through the generated Python SDK."""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "generated/open_perception_kit/python/src"))

from open_perception_kit.packet import ProducerIdentityStatus, decode  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.FrameContext import FrameContextT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.SemanticSegmentationMasks import (  # noqa: E402
    SemanticSegmentationMasksT,
)
from open_perception_kit.fb.open_perception_kit.metadata.InstanceSegmentationMasks import (  # noqa: E402
    InstanceSegmentationMasksT,
)
from open_perception_kit.fb.open_perception_kit.metadata.SegmentationMasks import (  # noqa: E402
    SegmentationMasksT,
)


def text(value: bytes | str) -> str:
    return value.decode("utf-8") if isinstance(value, bytes) else value


class Yolo26SemanticSdkRoundtripTests(unittest.TestCase):
    native_executable: Path

    def test_decodes_native_class_map_with_source_geometry_and_provenance(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            packet_path = Path(temporary_directory) / "semantic.bin"
            environment = os.environ.copy()
            environment["OPK_YOLO26_SEMANTIC_TEST_PACKET"] = str(packet_path)
            result = subprocess.run(
                [
                    str(self.native_executable),
                    "--gtest_filter=Yolo26SemanticSegmentationParser."
                    "SerializesClassMapAndSourceMetadata",
                ],
                env=environment,
                check=False,
                capture_output=True,
                text=True,
                timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue(packet_path.is_file(), result.stdout + result.stderr)
            envelope = decode(packet_path.read_bytes())

        self.assertTrue(envelope.valid(), envelope.error())
        self.assertIs(envelope.producer_identity(), ProducerIdentityStatus.EXACT_MATCH)
        self.assertEqual(envelope.count(InstanceSegmentationMasksT), 0)
        self.assertEqual(envelope.count(SegmentationMasksT), 0)
        frames = list(envelope.for_each(FrameContextT))
        self.assertEqual(len(frames), 1)
        self.assertEqual((frames[0].video.originalWidth, frames[0].video.originalHeight), (3, 2))
        source_id = frames[0].video.object.id

        # Select by payload type and content type, independently of envelope ordering.
        payloads = [
            payload
            for payload in envelope.for_each(SemanticSegmentationMasksT)
            if text(payload.layer.contentType) == "semanticSegmentation"
        ]
        self.assertEqual(len(payloads), 1)
        payload = payloads[0]
        self.assertEqual(payload.schemaMajor, 1)
        self.assertEqual(payload.schemaMinor, 0)
        self.assertEqual(text(payload.layer.labelFamily), "cityscapes")
        self.assertEqual(text(payload.layer.model), "yolo26n-sem")
        self.assertEqual(text(payload.layer.inferElementId), "infer-sem")
        self.assertIn(payload.layer.compositingMode, (None, b"", ""))
        self.assertEqual(text(payload.layer.producer.instanceId), "infer-sem/postprocessor")
        self.assertEqual(text(payload.layer.producer.component), "opk-std-ops/GenericPostprocess")
        self.assertEqual(
            text(payload.layer.producer.implementation), "Yolo26SemanticSegmentationParser"
        )
        self.assertEqual(len(payload.masks), 1)
        mask = payload.masks[0]
        self.assertGreater(mask.object.id, 0)
        self.assertEqual(mask.object.parentId, source_id)
        self.assertFalse(hasattr(mask, "classId"))
        self.assertFalse(hasattr(mask, "confidence"))
        self.assertFalse(hasattr(mask, "box"))
        self.assertEqual((mask.bitmap.width, mask.bitmap.height), (3, 2))
        self.assertEqual(text(mask.bitmap.valueType), "Uint8")
        self.assertEqual(list(mask.bitmap.pixels), [0, 1, 2, 8, 9, 10])


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(f"usage: {sys.argv[0]} <native-parser-test-executable>")
    Yolo26SemanticSdkRoundtripTests.native_executable = Path(sys.argv.pop(1)).resolve()
    unittest.main()
