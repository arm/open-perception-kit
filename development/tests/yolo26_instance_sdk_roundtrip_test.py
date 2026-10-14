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

"""Decode actual native YOLO26 parser output with the generated Python SDK."""

from __future__ import annotations

import hashlib
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "generated/open_perception_kit/python/src"))

from open_perception_kit import OPEN_PERCEPTION_KIT_VERSION  # noqa: E402
from open_perception_kit.packet import ProducerIdentityStatus, decode  # noqa: E402
from open_perception_kit.internalfb.WireEnvelope import WireEnvelope  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.FrameContext import FrameContextT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.SegmentationMasks import (  # noqa: E402
    SegmentationMasksT,
)
from open_perception_kit.fb.open_perception_kit.metadata.SemanticSegmentationMasks import (  # noqa: E402
    SemanticSegmentationMasksT,
)
from open_perception_kit.fb.open_perception_kit.metadata.InstanceSegmentationMasks import (  # noqa: E402
    InstanceSegmentationMasksT,
)


# Captured with checked-in SDK 0.1.1. The legacy SGMS contract and payload identity
# 0x1bbd8e0aef4d628f remain stable when semantic and instance roots are added.
HISTORICAL_MATTE_PACKET = bytes.fromhex(
    "18000000464c5744000000000c00140010000c00080004000c00000010000000540000005c000000"
    "70000000400000003162313934313864386130643334303338613363393938393566613933613131"
    "34306332356166393130663662633633633065313139373865313263323837360000000005000000"
    "302e312e31000000130000006f70656e5f70657263657074696f6e5f6b697400010000000c000000"
    "080014000800040008000000100000008f624def0a8ebd1b00000000e80000001800000053474d53"
    "000000000c000c0000000000080004000c000000080000008c000000010000000c00000008000c00"
    "080004000800000014000000400000000c00140010000c00080004000c0000001000000014000000"
    "0200000002000000040000000060b4ff0500000055696e7438000a001e0014000c0004000a000000"
    "0b0000000000000003000000000000000700000000000000000012000c0000000000000000000000"
    "08000400120000000800000020000000150000006261636b67726f756e645265706c6163656d656e"
    "740000000c0000007365676d656e746174696f6e00000000"
)


def text(value: bytes | str) -> str:
    return value.decode("utf-8") if isinstance(value, bytes) else value


class Yolo26InstanceSdkRoundtripTests(unittest.TestCase):
    native_executable: Path

    def test_preserves_historical_segmentation_as_a_typed_legacy_payload(self) -> None:
        self.assertEqual(
            hashlib.sha256(HISTORICAL_MATTE_PACKET).hexdigest(),
            "aa4d32645cabd776c2b300d401fbf1949d96979c697aa74c2de06d42b73cd64b",
        )
        envelope = decode(HISTORICAL_MATTE_PACKET)
        self.assertTrue(envelope.valid(), envelope.error())
        expected_identity = (
            ProducerIdentityStatus.SCHEMA_SET_MISMATCH
            if OPEN_PERCEPTION_KIT_VERSION == "0.1.1"
            else ProducerIdentityStatus.SDK_VERSION_MISMATCH
        )
        self.assertIs(envelope.producer_identity(), expected_identity)
        self.assertEqual(envelope.size(), 1)
        self.assertEqual(envelope.count(SegmentationMasksT), 1)
        self.assertEqual(envelope.count(InstanceSegmentationMasksT), 0)
        self.assertEqual(envelope.count(SemanticSegmentationMasksT), 0)
        legacy = list(envelope.for_each(SegmentationMasksT))[0]
        self.assertEqual(text(legacy.layer.contentType), "segmentation")
        self.assertEqual(text(legacy.layer.compositingMode), "backgroundReplacement")
        self.assertEqual(list(legacy.masks[0].bitmap.pixels), [0, 96, 180, 255])

        original = WireEnvelope.GetRootAs(HISTORICAL_MATTE_PACKET, 0)
        forwarded = WireEnvelope.GetRootAs(envelope.serialize(), 0)
        self.assertEqual(forwarded.PayloadsLength(), 1)
        old_entry = original.Payloads(0)
        forwarded_entry = forwarded.Payloads(0)
        self.assertEqual(old_entry.Id(), 0x1BBD8E0AEF4D628F)
        self.assertEqual(forwarded_entry.Id(), old_entry.Id())
        self.assertEqual(
            bytes(forwarded_entry.Blob(index) for index in range(forwarded_entry.BlobLength())),
            bytes(old_entry.Blob(index) for index in range(old_entry.BlobLength())),
        )

    def test_decodes_distinct_native_segmentation_payloads(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            packet_path = Path(temporary_directory) / "segmentation-payloads.bin"
            environment = os.environ.copy()
            environment["OPK_YOLO26_INSTANCE_TEST_PACKET"] = str(packet_path)
            result = subprocess.run(
                [
                    str(self.native_executable),
                    "--gtest_filter=Yolo26InstanceSegmentationParser."
                    "SerializesDistinctSegmentationPayloads",
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
        frames = list(envelope.for_each(FrameContextT))
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0].video.originalWidth, 4)
        self.assertEqual(frames[0].video.originalHeight, 4)
        source_id = frames[0].video.object.id

        self.assertEqual(envelope.size(), 4)
        self.assertEqual(envelope.count(InstanceSegmentationMasksT), 1)
        self.assertEqual(envelope.count(SemanticSegmentationMasksT), 1)
        self.assertEqual(envelope.count(SegmentationMasksT), 1)
        instance_payload = list(envelope.for_each(InstanceSegmentationMasksT))[0]
        semantic_payload = list(envelope.for_each(SemanticSegmentationMasksT))[0]
        matte_payload = list(envelope.for_each(SegmentationMasksT))[0]
        self.assertEqual(instance_payload.schemaMajor, 1)
        self.assertEqual(instance_payload.schemaMinor, 0)
        self.assertEqual(text(instance_payload.layer.contentType), "instanceSegmentation")
        self.assertEqual(text(instance_payload.layer.labelFamily), "coco")
        self.assertEqual(text(instance_payload.layer.model), "yolo26n-seg")
        self.assertEqual(text(instance_payload.layer.inferElementId), "infer-seg")
        self.assertEqual(
            text(instance_payload.layer.producer.implementation),
            "Yolo26InstanceSegmentationParser",
        )
        self.assertEqual(len(instance_payload.masks), 1)
        instance = instance_payload.masks[0]
        self.assertGreater(instance.object.id, 0)
        self.assertEqual(instance.object.parentId, source_id)
        self.assertEqual(instance.classId, 7)
        self.assertAlmostEqual(instance.confidence, 0.9, places=6)
        self.assertEqual(
            (instance.box.x, instance.box.y, instance.box.width, instance.box.height),
            (1.0, 1.0, 2.0, 2.0),
        )
        self.assertEqual((instance.bitmap.width, instance.bitmap.height), (4, 4))
        self.assertEqual(text(instance.bitmap.valueType), "Uint8")
        self.assertEqual(
            list(instance.bitmap.pixels),
            [0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0],
        )

        self.assertEqual(text(matte_payload.layer.contentType), "segmentation")
        self.assertEqual(text(matte_payload.layer.compositingMode), "backgroundReplacement")
        self.assertEqual(len(matte_payload.masks), 1)
        matte = matte_payload.masks[0]
        self.assertFalse(hasattr(matte, "classId"))
        self.assertFalse(hasattr(matte, "confidence"))
        self.assertFalse(hasattr(matte, "box"))
        self.assertEqual(matte.object.parentId, source_id)
        self.assertEqual(list(matte.bitmap.pixels), [0, 96, 180, 255])

        self.assertEqual(text(semantic_payload.layer.contentType), "semanticSegmentation")
        self.assertEqual(text(semantic_payload.layer.labelFamily), "cityscapes")
        self.assertEqual(len(semantic_payload.masks), 1)
        semantic = semantic_payload.masks[0]
        self.assertFalse(hasattr(semantic, "classId"))
        self.assertFalse(hasattr(semantic, "confidence"))
        self.assertFalse(hasattr(semantic, "box"))
        self.assertEqual(semantic.object.parentId, source_id)
        self.assertEqual((semantic.bitmap.width, semantic.bitmap.height), (4, 4))
        self.assertEqual(
            list(semantic.bitmap.pixels),
            [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 18],
        )


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(f"usage: {sys.argv[0]} <native-parser-test-executable>")
    Yolo26InstanceSdkRoundtripTests.native_executable = Path(sys.argv.pop(1)).resolve()
    unittest.main()
