################################################################
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
################################################################

from __future__ import annotations

import base64
import sys
import unittest
from pathlib import Path

import flatbuffers

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "plumber"))

from plumber.frame_results_decode import (  # noqa: E402
    FRAME_RESULTS_ENCODING,
    FrameResultsDecodeError,
    decode_frame_results_record,
)
from plumber.frame_results_sdk import FrameResults  # noqa: E402
from open_perception_kit import (  # noqa: E402
    OPEN_PERCEPTION_KIT_NAME,
    OPEN_PERCEPTION_KIT_VERSION,
    SCHEMA_SET_SHA256,
)
import open_perception_kit.internalfb.WireEnvelope as WireEnvelope  # noqa: E402


def make_record(packet: bytes) -> dict:
    return {
        "frame_counter": 7,
        "frame_results_encoding": FRAME_RESULTS_ENCODING,
        "frame_results_packet_b64": base64.b64encode(packet).decode("ascii"),
    }


def make_identity_packet(
    name: str | None,
    version: str | None,
    schema_set_sha256: str | None,
) -> bytes:
    builder = flatbuffers.Builder(0)
    name_offset = builder.CreateString(name) if name is not None else None
    version_offset = builder.CreateString(version) if version is not None else None
    schema_offset = (
        builder.CreateString(schema_set_sha256)
        if schema_set_sha256 is not None
        else None
    )

    WireEnvelope.WireEnvelopeStart(builder)
    if name_offset is not None:
        WireEnvelope.WireEnvelopeAddProducerSdkName(builder, name_offset)
    if version_offset is not None:
        WireEnvelope.WireEnvelopeAddProducerSdkVersion(builder, version_offset)
    if schema_offset is not None:
        WireEnvelope.WireEnvelopeAddProducerSchemaSetSha256(builder, schema_offset)
    envelope = WireEnvelope.WireEnvelopeEnd(builder)
    builder.Finish(envelope, file_identifier=b"FLWD")
    return bytes(builder.Output())


class FrameResultsDecodeTests(unittest.TestCase):
    def test_decodes_valid_empty_frame_results(self) -> None:
        record = make_record(FrameResults().serialize())

        frame = decode_frame_results_record(record)

        self.assertEqual(frame.frame_counter, 7)
        self.assertTrue(frame.frame_results.valid())
        self.assertEqual(frame.frame_results.size(), 0)

    def test_rejects_missing_packet(self) -> None:
        record = {
            "frame_counter": 1,
            "frame_results_encoding": FRAME_RESULTS_ENCODING,
        }

        with self.assertRaisesRegex(FrameResultsDecodeError, "frame_results_packet_b64"):
            decode_frame_results_record(record)

    def test_rejects_wrong_encoding(self) -> None:
        record = make_record(FrameResults().serialize())
        record["frame_results_encoding"] = "legacy-json"

        with self.assertRaisesRegex(FrameResultsDecodeError, "frame_results_encoding"):
            decode_frame_results_record(record)

    def test_rejects_invalid_base64(self) -> None:
        record = {
            "frame_counter": 1,
            "frame_results_encoding": FRAME_RESULTS_ENCODING,
            "frame_results_packet_b64": "not base64",
        }

        with self.assertRaisesRegex(FrameResultsDecodeError, "invalid frame_results_packet_b64"):
            decode_frame_results_record(record)

    def test_rejects_invalid_packet_bytes(self) -> None:
        record = make_record(b"not-a-frame-results-packet")

        with self.assertRaisesRegex(FrameResultsDecodeError, "invalid frame results packet"):
            decode_frame_results_record(record)

    def test_rejects_nonmatching_producer_identity(self) -> None:
        cases = [
            (None, None, None, "missing"),
            (OPEN_PERCEPTION_KIT_NAME, OPEN_PERCEPTION_KIT_VERSION, "bad", "malformed"),
            ("other_sdk", OPEN_PERCEPTION_KIT_VERSION, SCHEMA_SET_SHA256, "sdk_name_mismatch"),
            (OPEN_PERCEPTION_KIT_NAME, "9.9.9", SCHEMA_SET_SHA256, "sdk_version_mismatch"),
            (OPEN_PERCEPTION_KIT_NAME, OPEN_PERCEPTION_KIT_VERSION, "0" * 64, "schema_set_mismatch"),
        ]

        for name, version, schema_set_sha256, expected_status in cases:
            with self.subTest(status=expected_status):
                record = make_record(
                    make_identity_packet(name, version, schema_set_sha256)
                )
                with self.assertRaisesRegex(
                    FrameResultsDecodeError,
                    f"producer identity: {expected_status}",
                ):
                    decode_frame_results_record(record)


if __name__ == "__main__":
    unittest.main()
