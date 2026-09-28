# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

from __future__ import annotations

import base64
from dataclasses import dataclass
from typing import Any, Mapping

from .frame_results_sdk import FrameResults, ProducerIdentityStatus

FRAME_RESULTS_ENCODING = "perception-frame-results+base64"


class FrameResultsDecodeError(ValueError):
    pass


@dataclass(frozen=True)
class FrameResultsFrame:
    frame_counter: int | None
    ndjson_record: Mapping[str, Any]
    frame_results: FrameResults


def _decode_base64_packet(encoded_packet: Any) -> bytes:
    if not isinstance(encoded_packet, str) or not encoded_packet:
        raise FrameResultsDecodeError("missing or invalid frame_results_packet_b64")

    try:
        return base64.b64decode(encoded_packet, validate=True)
    except ValueError as exc:
        raise FrameResultsDecodeError("invalid frame_results_packet_b64") from exc


def decode_frame_results_record(ndjson_record: Mapping[str, Any]) -> FrameResultsFrame:
    encoding = ndjson_record.get("frame_results_encoding")
    if encoding != FRAME_RESULTS_ENCODING:
        raise FrameResultsDecodeError(
            f"unsupported frame_results_encoding: {encoding!r}")

    packet = _decode_base64_packet(ndjson_record.get("frame_results_packet_b64"))
    frame_results = FrameResults(packet)
    if not frame_results.valid():
        raise FrameResultsDecodeError(
            f"invalid frame results packet: {frame_results.error() or 'unknown error'}"
        )

    producer_identity = frame_results.producer_identity()
    if producer_identity is not ProducerIdentityStatus.EXACT_MATCH:
        raise FrameResultsDecodeError(
            "incompatible frame results producer identity: "
            f"{producer_identity.value}; "
            f"sdk={frame_results.producer_sdk_name!r}, "
            f"version={frame_results.producer_sdk_version!r}, "
            f"schema_set_sha256={frame_results.producer_schema_set_sha256!r}"
        )

    frame_counter = ndjson_record.get("frame_counter")
    if not isinstance(frame_counter, int):
        frame_counter = None

    return FrameResultsFrame(
        frame_counter=frame_counter,
        ndjson_record=ndjson_record,
        frame_results=frame_results,
    )
