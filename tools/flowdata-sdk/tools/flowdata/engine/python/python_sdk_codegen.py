# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

import re
import subprocess
import shutil
import tempfile
import textwrap
from pathlib import Path

from ..errors import fail
from ..flatbuffers_compat import FLATBUFFERS_PYTHON_REQUIREMENT
from ..python_compat import (
    PYTHON_BUILD_BACKEND,
    PYTHON_VERSION_REQUIREMENT,
)
from ..types import RESERVED_PAYLOAD_ID_MIN, GenerationContext, SchemaEntry

PYTHON_GENERATED_BANNER = (
    "# Generated file. Do not edit.\n"
    "# SDK users: change schemas or generator inputs, then regenerate this file.\n\n"
)

ENVELOPE_SCHEMA_TEMPLATE = """namespace __SDK_NAME__.internalfb;

table WirePayload {
  id:ulong;
  blob:[ubyte];
}

table WireEnvelope {
  payloads:[WirePayload];
  producer_sdk_name:string;
  producer_sdk_version:string;
  producer_schema_set_sha256:string;
}

root_type WireEnvelope;
file_identifier "FLWD";
"""


def _python_project_root(ctx: GenerationContext) -> Path:
    return getattr(ctx, "python_root", ctx.generated_root / "python")


def _python_namespace(namespace: str) -> str:
    return namespace.replace("::", ".")


def _python_fb_module(sdk_name: str, namespace: str, root_type_name: str) -> str:
    return f"{sdk_name}.fb.{_python_namespace(namespace)}.{root_type_name}"


def _flatc_include_args(include_dirs: list[Path]) -> list[str]:
    args: list[str] = []
    for include_dir in include_dirs:
        args.extend(["-I", str(include_dir)])
    return args


def _run_flatc_python(
    schemas: list[Path],
    output_dir: Path,
    flatc: str,
    *,
    include_dirs: list[Path] | None = None,
) -> None:
    include_dirs = include_dirs or []
    try:
        subprocess.run(
            [
                flatc,
                "--python",
                "--gen-object-api",
                "-o",
                str(output_dir),
                *_flatc_include_args(include_dirs),
                *(str(schema) for schema in schemas),
            ],
            check=True,
            text=True,
            capture_output=True,
        )
    except FileNotFoundError:
        fail(f"flatc not found: {flatc}")
    except subprocess.CalledProcessError as exc:
        details = "\n".join(
            part.strip()
            for part in [exc.stdout or "", exc.stderr or ""]
            if part.strip()
        )
        schema_list = ", ".join(str(schema) for schema in schemas)
        fail(
            f"flatc failed for schema set {schema_list} (exit code {exc.returncode})"
            + (f"\n{details}" if details else "")
        )


def _ensure_init_files(root: Path) -> list[Path]:
    files: list[Path] = []

    for directory in sorted(path for path in root.rglob("*") if path.is_dir()):
        if "__pycache__" in directory.parts:
            continue

        init = directory / "__init__.py"
        if not init.exists():
            init.write_text("", encoding="utf-8")
        files.append(init)

    return files


def _rewrite_schema_imports(fb_root: Path, sdk_name: str) -> None:
    top_level_names = sorted(
        path.name
        for path in fb_root.iterdir()
        if path.is_dir() and path.name != "__pycache__"
    )
    if not top_level_names:
        return

    top_level_pattern = "|".join(re.escape(name) for name in top_level_names)
    qualified_ref = re.compile(rf"(?<![A-Za-z0-9_\.])({top_level_pattern})\.")

    for module_path in sorted(fb_root.rglob("*.py")):
        text = module_path.read_text(encoding="utf-8")
        rewritten = qualified_ref.sub(
            lambda match: f"{sdk_name}.fb.{match.group(1)}.",
            text,
        )
        if rewritten != text:
            module_path.write_text(rewritten, encoding="utf-8")


def _registry_text(entries: list[SchemaEntry], sdk_name: str) -> str:
    lines = [
        PYTHON_GENERATED_BANNER.rstrip(),
        "_TYPE_REGISTRY = {",
    ]

    for entry in entries:
        module = _python_fb_module(sdk_name, entry.namespace, entry.root_type_name)

        lines.extend(
            [
                f"    {entry.numeric_id}: {{",
                f"        'name': '{entry.name}',",
                f"        'module': '{module}',",
                f"        'root_type': '{entry.root_type_name}',",
                f"        'native_type': '{entry.root_type_name}T',",
                f"        'qualified_root_type': '{entry.qualified_root_type}',",
                f"        'file_identifier': '{entry.file_identifier}',",
                "    },",
            ]
        )

    lines.append("}")

    return "\n".join(lines) + "\n"


def _sdk_text(
    sdk_name: str,
    python_package_name: str,
    sdk_version: str,
    schema_set_digest: str,
) -> str:
    return PYTHON_GENERATED_BANNER + textwrap.dedent(
        """
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
        SDK_NAME = "__SDK_NAME__"
        SDK_VERSION = "__SDK_VERSION__"
        SCHEMA_SET_SHA256 = "__SCHEMA_SET_SHA256__"
        EXTERNAL_KEY_MIN = __EXTERNAL_KEY_MIN__
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
            r"^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$"
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
                "__PYTHON_PACKAGE_NAME__.internalfb.WireEnvelope"
            )


        def _fb_payload_module():
            return importlib.import_module(
                "__PYTHON_PACKAGE_NAME__.internalfb.WirePayload"
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
        """
    ).lstrip().replace("__SDK_NAME__", sdk_name).replace(
        "__PYTHON_PACKAGE_NAME__", python_package_name
    ).replace(
        "__SDK_VERSION__", sdk_version
    ).replace(
        "__SCHEMA_SET_SHA256__", schema_set_digest
    ).replace(
        "__EXTERNAL_KEY_MIN__",
        str(RESERVED_PAYLOAD_ID_MIN),
    )


def _packet_text() -> str:
    return PYTHON_GENERATED_BANNER + textwrap.dedent(
        """
        from .sdk import (
            EXTERNAL_KEY_MIN,
            Envelope,
            ExternalKey,
            ProducerIdentityStatus,
            decode,
            external_key,
            is_external_key,
        )

        __all__ = [
            "EXTERNAL_KEY_MIN",
            "Envelope",
            "ExternalKey",
            "ProducerIdentityStatus",
            "decode",
            "external_key",
            "is_external_key",
        ]
        """
    ).lstrip()


def _guest_text(public_name: str) -> str:
    return PYTHON_GENERATED_BANNER + textwrap.dedent(
        """
        from .sdk import EXTERNAL_KEY_MIN, ExternalKey, ProducerIdentityStatus, external_key, is_external_key

        try:
            from __BRIDGE_MODULE__ import Envelope
        except ModuleNotFoundError as exc:
            if exc.name != "__BRIDGE_MODULE__":
                raise
            raise ImportError(
                "__SDK_NAME__.guest is available only inside a C++ host that registered "
                "the generated __BRIDGE_MODULE__ module"
            ) from exc

        __all__ = [
            "EXTERNAL_KEY_MIN",
            "Envelope",
            "ExternalKey",
            "ProducerIdentityStatus",
            "external_key",
            "is_external_key",
        ]
        """
    ).lstrip().replace("__SDK_NAME__", public_name).replace(
        "__BRIDGE_MODULE__",
        f"{public_name}_bridge",
    )


def _guest_stub_text() -> str:
    return PYTHON_GENERATED_BANNER + textwrap.dedent(
        """
        from typing import TypeVar, overload

        from .sdk import ExternalKey, ProducerIdentityStatus, external_key, is_external_key

        _PayloadT = TypeVar("_PayloadT")


        class Envelope:
            @property
            def producer_sdk_name(self) -> str: ...

            @property
            def producer_sdk_version(self) -> str: ...

            @property
            def producer_schema_set_sha256(self) -> str: ...

            def valid(self) -> bool: ...
            def producer_identity(self) -> ProducerIdentityStatus: ...

            @overload
            def count(self, selector: type[_PayloadT]) -> int: ...
            @overload
            def count(self, selector: ExternalKey) -> int: ...

            @overload
            def contains(self, selector: type[_PayloadT]) -> bool: ...
            @overload
            def contains(self, selector: ExternalKey) -> bool: ...

            @overload
            def get(self, selector: type[_PayloadT], index: int = 0) -> _PayloadT | None: ...
            @overload
            def get(self, selector: ExternalKey, index: int = 0) -> bytes | None: ...

            @overload
            def for_each(self, selector: type[_PayloadT]) -> list[_PayloadT]: ...
            @overload
            def for_each(self, selector: ExternalKey) -> list[bytes]: ...

            @overload
            def add(self, value: _PayloadT) -> None: ...
            @overload
            def add(
                self,
                value: ExternalKey,
                blob: bytes | bytearray | memoryview,
            ) -> None: ...
        """
    ).lstrip()


def generate_python_sdk(entries: list[SchemaEntry], ctx: GenerationContext) -> list[Path]:
    from ..schema_set import schema_set_sha256

    schema_set_digest = schema_set_sha256(ctx)
    project_root = _python_project_root(ctx)
    src_root = project_root / "src"
    python_package_name = ctx.effective_public_name
    package_root = src_root / python_package_name
    fb_root = package_root / "fb"
    if src_root.exists():
        shutil.rmtree(src_root)
    package_root.mkdir(parents=True, exist_ok=True)

    generated: list[Path] = []

    with tempfile.TemporaryDirectory(prefix=f"{ctx.sdk_name}-python-envelope-") as tmp:
        envelope_schema = Path(tmp) / "envelope.fbs"
        envelope_schema.write_text(
            ENVELOPE_SCHEMA_TEMPLATE.replace("__SDK_NAME__", python_package_name),
            encoding="utf-8",
        )
        _run_flatc_python([envelope_schema], src_root, ctx.flatc_bin)

    fb_root.mkdir(parents=True, exist_ok=True)
    _run_flatc_python(ctx.schema_paths, fb_root, ctx.flatc_bin, include_dirs=[ctx.schema_dir])
    _rewrite_schema_imports(fb_root, python_package_name)

    generated.extend(sorted(path for path in src_root.rglob("*.py") if "__pycache__" not in path.parts))
    generated.extend(_ensure_init_files(src_root))

    init_path = package_root / "__init__.py"
    version_constant = f"{ctx.effective_public_name.upper()}_VERSION"
    init_path.write_text(
        PYTHON_GENERATED_BANNER
        + f'{version_constant} = "{ctx.sdk_version}"\n'
        + f'{ctx.effective_public_name.upper()}_NAME = "{ctx.effective_public_name}"\n'
        + f'SCHEMA_SET_SHA256 = "{schema_set_digest}"\n'
        + f'FLATBUFFERS_VERSION_REQUIREMENT = "{FLATBUFFERS_PYTHON_REQUIREMENT}"\n'
        + f"__version__ = {version_constant}\n\n"
        + "from .sdk import (\n"
        + "    EXTERNAL_KEY_MIN,\n"
        + "    ExternalKey,\n"
        + "    ProducerIdentityStatus,\n"
        + "    external_key,\n"
        + "    is_external_key,\n"
        + ")\n",
        encoding="utf-8",
    )
    generated.append(init_path)

    packet_path = package_root / "packet.py"
    packet_path.write_text(_packet_text(), encoding="utf-8")
    generated.append(packet_path)

    guest_path = package_root / "guest.py"
    guest_path.write_text(
        _guest_text(python_package_name), encoding="utf-8"
    )
    generated.append(guest_path)

    guest_stub_path = package_root / "guest.pyi"
    guest_stub_path.write_text(_guest_stub_text(), encoding="utf-8")
    generated.append(guest_stub_path)

    typing_marker_path = package_root / "py.typed"
    typing_marker_path.write_text("", encoding="utf-8")
    generated.append(typing_marker_path)

    registry_path = package_root / "registry.py"
    registry_path.write_text(
        _registry_text(entries, python_package_name),
        encoding="utf-8",
    )
    generated.append(registry_path)

    sdk_path = package_root / "sdk.py"
    sdk_path.write_text(
        _sdk_text(
            ctx.effective_public_name,
            python_package_name,
            str(ctx.sdk_version),
            schema_set_digest,
        ),
        encoding="utf-8",
    )
    generated.append(sdk_path)

    pyproject_path = project_root / "pyproject.toml"
    pyproject_path.write_text(
        "# Generated file. Do not edit.\n"
        "# SDK users: change schemas or generator inputs, then regenerate this file.\n\n"
        + textwrap.dedent(
            """
            [project]
            name = "__SDK_NAME__"
            version = "__SDK_VERSION__"
            description = "Generated __SDK_NAME__ Python SDK"
            requires-python = "__PYTHON_VERSION_REQUIREMENT__"
            dependencies = ["flatbuffers__FLATBUFFERS_VERSION_REQUIREMENT__"]

            [build-system]
            requires = ["setuptools"]
            build-backend = "__PYTHON_BUILD_BACKEND__"

            [tool.setuptools.packages.find]
            where = ["src"]

            [tool.setuptools.package-data]
            "*" = ["*.pyi", "py.typed"]
            """
        ).lstrip().replace("__SDK_NAME__", python_package_name).replace(
            "__SDK_VERSION__", str(ctx.sdk_version)
        ).replace(
            "__PYTHON_VERSION_REQUIREMENT__", PYTHON_VERSION_REQUIREMENT
        ).replace(
            "__PYTHON_BUILD_BACKEND__", PYTHON_BUILD_BACKEND
        ).replace(
            "__FLATBUFFERS_VERSION_REQUIREMENT__", FLATBUFFERS_PYTHON_REQUIREMENT
        ),
        encoding="utf-8",
    )
    generated.append(pyproject_path)

    return sorted(set(generated))
