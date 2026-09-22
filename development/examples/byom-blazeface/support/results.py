################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

"""Decode opkcomm records into the caller-owned face result contract."""

from __future__ import annotations

import base64
from dataclasses import dataclass
import json
import math
from typing import Any

from perception import ProducerIdentityStatus
from perception.packet import decode, external_key

from support.runtime import ExampleError


FRAME_RESULTS_ENCODING = "perception-frame-results+base64"
FACES_KEY_NAME = "com.arm.example.blazeface.faces.v1"
FACES_KEY = external_key(FACES_KEY_NAME)
FACE_FIELDS = {"x", "y", "width", "height", "confidence"}


@dataclass(frozen=True)
class Face:
    x: float
    y: float
    width: float
    height: float
    confidence: float


@dataclass(frozen=True)
class FrameFaces:
    frame: int
    faces: tuple[Face, ...]


class FaceResultDecoder:
    """Own the transport, SDK-identity, external-key, and JSON boundaries."""

    def decode_record(self, line: bytes, record_number: int) -> FrameFaces:
        wrapper = _decode_wrapper(line, record_number)
        frame = _frame_counter(wrapper, record_number)
        packet = _decode_packet_bytes(wrapper, frame)

        try:
            envelope = decode(packet)
        except Exception as exc:
            raise ExampleError(f"frame {frame}: packet decode failed: {exc}") from exc
        if not envelope.valid():
            raise ExampleError(f"frame {frame}: invalid FrameResults packet: {envelope.error()}")

        producer_identity = envelope.producer_identity()
        if producer_identity is not ProducerIdentityStatus.EXACT_MATCH:
            identity_value = getattr(producer_identity, "value", str(producer_identity))
            raise ExampleError(
                f"frame {frame}: incompatible FrameResults producer identity: "
                f"{identity_value}; sdk={envelope.producer_sdk_name!r}, "
                f"version={envelope.producer_sdk_version!r}, "
                f"schema_set_sha256={envelope.producer_schema_set_sha256!r}"
            )

        payloads = list(envelope.for_each(FACES_KEY))
        if len(payloads) != 1:
            raise ExampleError(
                f"frame {frame}: expected one {FACES_KEY_NAME} payload, got {len(payloads)}"
            )
        payload = _decode_json_payload(payloads[0], frame)
        return FrameFaces(frame=frame, faces=_validate_faces(payload, frame))


def print_frame(result: FrameFaces) -> None:
    noun = "face" if len(result.faces) == 1 else "faces"
    print(f"frame {result.frame}: {len(result.faces)} {noun}")
    for index, face in enumerate(result.faces, start=1):
        print(
            f"  face {index}: x={face.x:.3f} y={face.y:.3f} "
            f"width={face.width:.3f} height={face.height:.3f} "
            f"confidence={face.confidence:.3f}"
        )


def _decode_wrapper(line: bytes, record_number: int) -> dict[str, Any]:
    try:
        text = line.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ExampleError(f"record {record_number}: NDJSON is not valid UTF-8") from exc
    try:
        wrapper = json.loads(text)
    except json.JSONDecodeError as exc:
        raise ExampleError(f"record {record_number}: malformed NDJSON: {exc}") from exc
    if not isinstance(wrapper, dict):
        raise ExampleError(f"record {record_number}: NDJSON wrapper must be an object")
    return wrapper


def _frame_counter(wrapper: dict[str, Any], record_number: int) -> int:
    frame = wrapper.get("frame_counter")
    if isinstance(frame, bool) or not isinstance(frame, int) or frame < 0:
        raise ExampleError(f"record {record_number}: invalid frame_counter {frame!r}")
    return frame


def _decode_packet_bytes(wrapper: dict[str, Any], frame: int) -> bytes:
    encoding = wrapper.get("frame_results_encoding")
    if encoding != FRAME_RESULTS_ENCODING:
        raise ExampleError(f"frame {frame}: unsupported frame_results_encoding {encoding!r}")
    encoded_packet = wrapper.get("frame_results_packet_b64")
    if not isinstance(encoded_packet, str) or not encoded_packet:
        raise ExampleError(f"frame {frame}: missing frame_results_packet_b64")
    try:
        return base64.b64decode(encoded_packet, validate=True)
    except ValueError as exc:
        raise ExampleError(f"frame {frame}: invalid frame_results_packet_b64") from exc


def _decode_json_payload(payload: bytes, frame: int) -> Any:
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise ExampleError(f"frame {frame}: external payload is not valid UTF-8") from exc
    try:
        return json.loads(text)
    except json.JSONDecodeError as exc:
        raise ExampleError(f"frame {frame}: external payload is malformed JSON: {exc}") from exc


def _validate_faces(payload: Any, frame: int) -> tuple[Face, ...]:
    if not isinstance(payload, dict) or set(payload) != {"faces"}:
        raise ExampleError(f"frame {frame}: external payload must contain only 'faces'")
    raw_faces = payload["faces"]
    if not isinstance(raw_faces, list):
        raise ExampleError(f"frame {frame}: external payload 'faces' must be a list")

    return tuple(
        _validated_face(raw_face, frame, index)
        for index, raw_face in enumerate(raw_faces, start=1)
    )


def _validated_face(raw_face: Any, frame: int, index: int) -> Face:
    if not isinstance(raw_face, dict) or set(raw_face) != FACE_FIELDS:
        raise ExampleError(f"frame {frame}: face {index} has an invalid structure")
    face = Face(
        x=_number(raw_face["x"], "x", frame),
        y=_number(raw_face["y"], "y", frame),
        width=_number(raw_face["width"], "width", frame),
        height=_number(raw_face["height"], "height", frame),
        confidence=_number(raw_face["confidence"], "confidence", frame),
    )
    _validate_face_ranges(face, frame, index)
    return face


def _validate_face_ranges(face: Face, frame: int, index: int) -> None:
    for field, value in (
        ("x", face.x),
        ("y", face.y),
        ("width", face.width),
        ("height", face.height),
        ("confidence", face.confidence),
    ):
        if not 0.0 <= value <= 1.0:
            raise ExampleError(
                f"frame {frame}: face {index} field {field!r} is outside [0,1]"
            )
    if face.width <= 0.0 or face.height <= 0.0:
        raise ExampleError(f"frame {frame}: face {index} has an empty rectangle")
    if face.x + face.width > 1.0 + 1e-9:
        raise ExampleError(f"frame {frame}: face {index} exceeds the frame width")
    if face.y + face.height > 1.0 + 1e-9:
        raise ExampleError(f"frame {frame}: face {index} exceeds the frame height")


def _number(value: Any, field: str, frame: int) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ExampleError(f"frame {frame}: face field {field!r} must be numeric")
    result = float(value)
    if not math.isfinite(result):
        raise ExampleError(f"frame {frame}: face field {field!r} must be finite")
    return result
