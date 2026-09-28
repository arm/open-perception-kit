################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

# Generated file. Do not edit.
# SDK users: change schemas or generator inputs, then regenerate this file.

from __future__ import annotations

import importlib
import re
import struct
from dataclasses import dataclass
from enum import Enum
from typing import Any, Iterator

import flatbuffers

from .registry import _TYPE_REGISTRY

ENVELOPE_FILE_IDENTIFIER = b"FLWD"
SDK_NAME = "open_perception_kit"
SDK_VERSION = "0.1.0"
SCHEMA_SET_SHA256 = "1b19418d8a0d34038a3c99895fa93a1140c25af910f6bc63c0e11978e12c2876"
EXTERNAL_KEY_MIN = 9223372036854775808
_EXTERNAL_KEY_MASK = EXTERNAL_KEY_MIN - 1
_EXTERNAL_HASH_OFFSET = 14695981039346656037
_EXTERNAL_HASH_PRIME = 1099511628211
_UINT64_MASK = 0xffffffffffffffff


@dataclass(frozen=True)
class PayloadEntry:
    id: int
    blob: bytes


class ProducerIdentityStatus(str, Enum):
    EXACT_MATCH = "exact_match"
    MISSING = "missing"
    MALFORMED = "malformed"
    SDK_NAME_MISMATCH = "sdk_name_mismatch"
    SDK_VERSION_MISMATCH = "sdk_version_mismatch"
    SCHEMA_SET_MISMATCH = "schema_set_mismatch"


_SDK_NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")
_SEMANTIC_VERSION_RE = re.compile(
    r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$"
)
_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


_EXTERNAL_KEY_TOKEN = object()


def _is_external_key_value(value: int) -> bool:
    return EXTERNAL_KEY_MIN <= value <= _UINT64_MASK


class ExternalKey:
    __slots__ = ("__value",)

    def __init__(self, value: int, token: object | None = None) -> None:
        if token is not _EXTERNAL_KEY_TOKEN:
            raise TypeError("external keys must be created with external_key()")

        value = int(value)
        if not _is_external_key_value(value):
            raise ValueError(f"{SDK_NAME} external key is outside the external key domain")
        self.__value = value

    @property
    def value(self) -> int:
        return self.__value

    def __int__(self) -> int:
        return self.__value

    def __hash__(self) -> int:
        return hash(self.__value)

    def __eq__(self, other: object) -> bool:
        return isinstance(other, ExternalKey) and self.__value == other.__value

    def __repr__(self) -> str:
        return f"ExternalKey({self.__value})"


def external_key(key: str) -> ExternalKey:
    if not isinstance(key, str):
        raise TypeError(f"expected external key string, got {type(key)!r}")

    value = _EXTERNAL_HASH_OFFSET
    for byte in key.encode("utf-8"):
        value ^= byte
        value = (value * _EXTERNAL_HASH_PRIME) & _UINT64_MASK
    return ExternalKey((value & _EXTERNAL_KEY_MASK) | EXTERNAL_KEY_MIN, _EXTERNAL_KEY_TOKEN)


def is_external_key(key: object) -> bool:
    return isinstance(key, ExternalKey) and _is_external_key_value(key.value)


def _resolve_external_key(key: ExternalKey) -> int:
    if isinstance(key, ExternalKey):
        return key.value

    if isinstance(key, str):
        raise TypeError(
            f"{SDK_NAME} external envelope operations require ExternalKey; "
            "call external_key(...) first"
        )

    raise TypeError(
        f"{SDK_NAME} external key must be a value returned by external_key()"
    )


def _entry(type_id: int) -> dict[str, str]:
    try:
        return _TYPE_REGISTRY[int(type_id)]
    except KeyError as exc:
        raise KeyError(f"unknown {SDK_NAME} payload id: {type_id}") from exc


def _root_class(type_id: int):
    entry = _entry(type_id)
    return getattr(importlib.import_module(entry["module"]), entry["root_type"])


def _native_class(type_id: int):
    entry = _entry(type_id)
    return getattr(importlib.import_module(entry["module"]), entry["native_type"])


def _native_type_key(obj: Any) -> tuple[str, str]:
    cls = type(obj)
    return cls.__module__, cls.__name__


_NATIVE_TYPE_TO_ID = {
    (entry["module"], entry["native_type"]): type_id
    for type_id, entry in _TYPE_REGISTRY.items()
}


def _payload_type_key(payload_type: type) -> tuple[str, str]:
    return payload_type.__module__, payload_type.__name__


def _resolve_payload_type(payload_type: type) -> int:
    if not isinstance(payload_type, type):
        raise TypeError(
            f"{SDK_NAME} selector must be a generated payload type or ExternalKey"
        )
    try:
        return _NATIVE_TYPE_TO_ID[_payload_type_key(payload_type)]
    except KeyError as exc:
        raise KeyError(
            f"unknown {SDK_NAME} payload type: "
            f"{payload_type.__module__}.{payload_type.__name__}"
        ) from exc


def _decode_payload_blob(type_id: int, blob: bytes):
    root_cls = _root_class(type_id)
    blob_bytes = bytes(blob)
    check_name = f"{root_cls.__name__}BufferHasIdentifier"
    check_identifier = getattr(root_cls, check_name, None)
    if check_identifier is not None and not check_identifier(blob_bytes, 0):
        expected = _entry(type_id)["file_identifier"]
        raise ValueError(
            f"{SDK_NAME} payload file_identifier mismatch for id {type_id}: "
            f"expected {expected}"
        )
    return root_cls.GetRootAs(blob_bytes, 0)


def _unpack_payload_blob(type_id: int, blob: bytes):
    raw = _decode_payload_blob(type_id, blob)
    return _native_class(type_id).InitFromObj(raw)


def _try_unpack_payload_blob(type_id: int, blob: bytes):
    try:
        return _unpack_payload_blob(type_id, blob)
    except (ValueError, TypeError, IndexError, struct.error):
        return None


def _pack_native_payload(type_id: int, value: Any) -> bytes:
    if not hasattr(value, "Pack"):
        raise TypeError(f"expected FlatBuffers object-api native object, got {type(value)!r}")

    builder = flatbuffers.Builder(0)
    offset = value.Pack(builder)
    builder.Finish(offset, file_identifier=_entry(type_id)["file_identifier"].encode("ascii"))
    return bytes(builder.Output())


def _fb_envelope_module():
    return importlib.import_module(
        "open_perception_kit.internalfb.WireEnvelope"
    )


def _fb_payload_module():
    return importlib.import_module(
        "open_perception_kit.internalfb.WirePayload"
    )


def _copy_payload_blob(payload: Any) -> bytes:
    try:
        blob = payload.BlobAsNumpy()
        if hasattr(blob, "tobytes"):
            return blob.tobytes()
    except Exception:
        pass
    return bytes(payload.Blob(index) for index in range(payload.BlobLength()))


def _metadata_text(value: Any) -> str:
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="surrogateescape")
    return str(value)


class Envelope:
    def __init__(self, packet: bytes | bytearray | memoryview | None = None) -> None:
        self._payloads: list[PayloadEntry] = []
        self._error: str | None = None
        self._valid = True
        self._producer_sdk_name = SDK_NAME
        self._producer_sdk_version = SDK_VERSION
        self._producer_schema_set_sha256 = SCHEMA_SET_SHA256

        if packet is not None:
            self._load(packet)

    def _load(self, packet: bytes | bytearray | memoryview) -> None:
        self._payloads.clear()
        self._valid = False
        self._producer_sdk_name = ""
        self._producer_sdk_version = ""
        self._producer_schema_set_sha256 = ""
        packet_bytes = bytes(packet)

        try:
            fb_envelope = _fb_envelope_module().WireEnvelope
            if not fb_envelope.WireEnvelopeBufferHasIdentifier(packet_bytes, 0):
                self._error = f"invalid {SDK_NAME} envelope file_identifier"
                return

            root = fb_envelope.GetRootAs(packet_bytes, 0)
            self._producer_sdk_name = _metadata_text(root.ProducerSdkName())
            self._producer_sdk_version = _metadata_text(root.ProducerSdkVersion())
            self._producer_schema_set_sha256 = _metadata_text(
                root.ProducerSchemaSetSha256()
            )

            for index in range(root.PayloadsLength()):
                payload = root.Payloads(index)
                if payload is None:
                    continue
                self._payloads.append(PayloadEntry(payload.Id(), _copy_payload_blob(payload)))

            self._valid = True
            self._error = None
        except Exception as exc:
            self._payloads.clear()
            self._error = f"invalid {SDK_NAME} envelope: {exc}"

    def valid(self) -> bool:
        return self._valid

    def error(self) -> str | None:
        return self._error

    @property
    def producer_sdk_name(self) -> str:
        return self._producer_sdk_name

    @property
    def producer_sdk_version(self) -> str:
        return self._producer_sdk_version

    @property
    def producer_schema_set_sha256(self) -> str:
        return self._producer_schema_set_sha256

    def producer_identity(self) -> ProducerIdentityStatus:
        if not (
            self._producer_sdk_name
            and self._producer_sdk_version
            and self._producer_schema_set_sha256
        ):
            return ProducerIdentityStatus.MISSING
        if not (
            _SDK_NAME_RE.fullmatch(self._producer_sdk_name)
            and _SEMANTIC_VERSION_RE.fullmatch(self._producer_sdk_version)
            and _SHA256_RE.fullmatch(self._producer_schema_set_sha256)
        ):
            return ProducerIdentityStatus.MALFORMED
        if self._producer_sdk_name != SDK_NAME:
            return ProducerIdentityStatus.SDK_NAME_MISMATCH
        if self._producer_sdk_version != SDK_VERSION:
            return ProducerIdentityStatus.SDK_VERSION_MISMATCH
        if self._producer_schema_set_sha256 != SCHEMA_SET_SHA256:
            return ProducerIdentityStatus.SCHEMA_SET_MISMATCH
        return ProducerIdentityStatus.EXACT_MATCH

    def add(self, value: Any, blob: bytes | bytearray | memoryview | None = None) -> None:
        if not self._valid:
            raise RuntimeError(
                f"cannot add to invalid {SDK_NAME} envelope: {self._error or 'unknown error'}"
            )

        if isinstance(value, ExternalKey):
            if blob is None:
                raise TypeError(f"{SDK_NAME} external add requires a bytes-like blob")
            self._payloads.append(PayloadEntry(_resolve_external_key(value), bytes(blob)))
            return

        if blob is not None:
            raise TypeError(
                f"{SDK_NAME} external add requires ExternalKey; call external_key(...) first"
            )

        inferred_id = _NATIVE_TYPE_TO_ID.get(_native_type_key(value))
        if inferred_id is None:
            raise TypeError(
                f"cannot infer {SDK_NAME} payload type for "
                f"{type(value).__module__}.{type(value).__name__}"
            )

        self._payloads.append(
            PayloadEntry(inferred_id, _pack_native_payload(inferred_id, value))
        )

    def empty(self) -> bool:
        '''Return True if the envelope contains no payload entries.'''
        return len(self._payloads) == 0

    def size(self) -> int:
        '''Return number of payload entries in the envelope.'''
        return len(self._payloads) if self._valid else 0

    def count(self, selector: type | ExternalKey) -> int:
        if not self._valid:
            return 0
        if isinstance(selector, ExternalKey):
            type_id = _resolve_external_key(selector)
            return sum(1 for payload in self._payloads if payload.id == type_id)

        type_id = _resolve_payload_type(selector)
        return sum(
            1
            for payload in self._payloads
            if payload.id == type_id
            and _try_unpack_payload_blob(type_id, payload.blob) is not None
        )

    def contains(self, selector: type | ExternalKey) -> bool:
        return self.count(selector) > 0

    def _native_at_id(self, type_id: int, index: int):
        seen = 0
        for payload in self._payloads:
            if payload.id != type_id:
                continue
            value = _try_unpack_payload_blob(type_id, payload.blob)
            if value is None:
                continue
            if seen == index:
                return value
            seen += 1
        return None

    def _external_at_id(self, type_id: int, index: int) -> bytes | None:
        if not self._valid:
            return None
        seen = 0
        for payload in self._payloads:
            if payload.id != type_id:
                continue
            if seen == index:
                return bytes(payload.blob)
            seen += 1
        return None

    def get(self, selector: type | ExternalKey, index: int = 0):
        '''Return the nth known payload object or external bytes for `selector`.'''
        if isinstance(selector, ExternalKey):
            return self._external_at_id(_resolve_external_key(selector), index)

        type_id = _resolve_payload_type(selector)
        return self._native_at_id(type_id, index)

    def for_each(self, selector: type | ExternalKey) -> Iterator[Any]:
        if isinstance(selector, ExternalKey):
            type_id = _resolve_external_key(selector)
            if not self._valid:
                return
            for payload in self._payloads:
                if payload.id == type_id:
                    yield bytes(payload.blob)
            return

        type_id = _resolve_payload_type(selector)
        for payload in self._payloads:
            if payload.id != type_id:
                continue
            value = _try_unpack_payload_blob(type_id, payload.blob)
            if value is not None:
                yield value

    def serialize(self) -> bytes:
        if not self._valid:
            raise RuntimeError(
                f"cannot serialize invalid {SDK_NAME} envelope: {self._error or 'unknown error'}"
            )

        fb_envelope = _fb_envelope_module()
        fb_payload = _fb_payload_module()

        builder = flatbuffers.Builder(0)
        payload_offsets = []

        for payload in self._payloads:
            blob_offset = builder.CreateByteVector(payload.blob)

            fb_payload.WirePayloadStart(builder)
            fb_payload.WirePayloadAddId(builder, int(payload.id))
            fb_payload.WirePayloadAddBlob(builder, blob_offset)
            payload_offsets.append(fb_payload.WirePayloadEnd(builder))

        fb_envelope.WireEnvelopeStartPayloadsVector(builder, len(payload_offsets))
        for payload_offset in reversed(payload_offsets):
            builder.PrependUOffsetTRelative(payload_offset)
        payloads_vector = builder.EndVector()

        producer_sdk_name = builder.CreateString(SDK_NAME)
        producer_sdk_version = builder.CreateString(SDK_VERSION)
        producer_schema_set_sha256 = builder.CreateString(SCHEMA_SET_SHA256)

        fb_envelope.WireEnvelopeStart(builder)
        fb_envelope.WireEnvelopeAddPayloads(builder, payloads_vector)
        fb_envelope.WireEnvelopeAddProducerSdkName(builder, producer_sdk_name)
        fb_envelope.WireEnvelopeAddProducerSdkVersion(builder, producer_sdk_version)
        fb_envelope.WireEnvelopeAddProducerSchemaSetSha256(
            builder, producer_schema_set_sha256
        )
        envelope = fb_envelope.WireEnvelopeEnd(builder)

        builder.Finish(envelope, file_identifier=ENVELOPE_FILE_IDENTIFIER)
        return bytes(builder.Output())


def decode(packet: bytes | bytearray | memoryview) -> Envelope:
    return Envelope(packet)
