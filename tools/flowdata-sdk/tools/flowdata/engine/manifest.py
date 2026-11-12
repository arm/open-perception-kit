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

import hashlib
import json
import re
from pathlib import Path, PurePosixPath, PureWindowsPath
from typing import Any

from .errors import fail
from .flatbuffers_compat import (
    FLATBUFFERS_SUPPORTED_VERSION_REQUIREMENT,
    flatbuffers_runtime_contracts,
    is_supported_flatbuffers_version,
    parse_flatbuffers_version,
)
from .python_compat import (
    PYTHON_BUILD_BACKEND,
    PYTHON_VERSION_REQUIREMENT,
    PYTHON_WHEEL_TAG,
)
from .schema_set import schema_file_records, schema_paths_sha256, schema_set_sha256
from .types import GenerationContext, SchemaEntry
from .version import GENERATOR_VERSION


MANIFEST_FILENAME = "flowdata-manifest.json"
_SDK_NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")
_SEMANTIC_VERSION_RE = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$", re.ASCII
)
_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
_PAYLOAD_ID_RE = re.compile(r"^0x[0-9a-f]{16}$")
_SUPPORTED_SDKS = {"cpp", "python", "rust", "ts"}
_SUPPORTED_INTEGRATIONS = {"cmake", "meson"}


def python_package_descriptor(sdk_name: str, sdk_version: str) -> dict[str, Any]:
    return {
        "build_backend": PYTHON_BUILD_BACKEND,
        "distribution_name": sdk_name,
        "import_name": sdk_name,
        "pure_python": True,
        "requires_python": PYTHON_VERSION_REQUIREMENT,
        "typing": {
            "marker": f"src/{sdk_name}/py.typed",
            "stubs": [f"src/{sdk_name}/guest.pyi"],
        },
        "version": sdk_version,
        "wheel_tag": PYTHON_WHEEL_TAG,
    }


def python_bridge_descriptor(sdk_name: str) -> dict[str, Any]:
    return {
        "header": f"python_bridge/{sdk_name}_python_bridge.h",
        "module_name": f"{sdk_name}_bridge",
        "registration_function": f"{sdk_name}::python_bridge::append_inittab",
        "requires_python": PYTHON_VERSION_REQUIREMENT,
        "sdk_import_name": sdk_name,
        "source": f"python_bridge/{sdk_name}_python_bridge.cpp",
        "wrapper_type": f"{sdk_name}::python_bridge::scoped_envelope",
    }


def manifest_root(context: GenerationContext, sdk_kind: str) -> Path:
    if sdk_kind == "cpp":
        return context.cpp_root
    if sdk_kind == "python":
        return context.python_root
    if sdk_kind == "ts":
        return context.typescript_root
    if sdk_kind == "rust":
        return context.rust_root
    fail(f"cannot determine manifest root for unsupported SDK: {sdk_kind}")


def manifest_path(context: GenerationContext, sdk_kind: str) -> Path:
    return manifest_root(context, sdk_kind) / MANIFEST_FILENAME


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _manifest_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _require_object(value: Any, label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be a JSON object")
    return value


def _require_keys(value: dict[str, Any], expected: set[str], label: str) -> None:
    actual = set(value)
    if actual != expected:
        missing = sorted(expected - actual)
        unexpected = sorted(actual - expected)
        details: list[str] = []
        if missing:
            details.append("missing " + ", ".join(missing))
        if unexpected:
            details.append("unexpected " + ", ".join(unexpected))
        raise ValueError(f"{label} has invalid fields: {'; '.join(details)}")


def _require_string(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value:
        raise ValueError(f"{label} must be a non-empty string")
    return value


def _require_sha256(value: Any, label: str) -> str:
    text = _require_string(value, label)
    if _SHA256_RE.fullmatch(text) is None:
        raise ValueError(f"{label} must be a lowercase 64-character SHA-256 digest")
    return text


def _relative_manifest_path(value: Any, label: str) -> PurePosixPath:
    text = _require_string(value, label)
    path = PurePosixPath(text)
    if (
        "\\" in text
        or path.is_absolute()
        or PureWindowsPath(text).is_absolute()
        or path == PurePosixPath(".")
        or any(part in {"", ".", ".."} for part in path.parts)
    ):
        raise ValueError(f"{label} must be a normalized relative POSIX path")
    return path


def _resolved_manifest_file(root: Path, relative_path: PurePosixPath, label: str) -> Path:
    resolved_root = root.resolve()
    target = root.joinpath(*relative_path.parts)
    try:
        resolved_target = target.resolve(strict=True)
    except FileNotFoundError:
        raise ValueError(f"{label} does not exist: {target}") from None
    try:
        resolved_target.relative_to(resolved_root)
    except ValueError:
        raise ValueError(f"{label} escapes its logical root: {target}") from None
    if not resolved_target.is_file():
        raise ValueError(f"{label} is not a regular file: {target}")
    return resolved_target


def verify_generation_manifest(
    manifest_file: Path,
    schema_root: Path | None = None,
) -> dict[str, Any]:
    from .manifest_contract import verify_output_contract, verify_payloads
    from .manifest_files import verify_output_files, verify_schema_files

    manifest_path = manifest_file.resolve()
    try:
        manifest = json.loads(
            manifest_path.read_text(encoding="utf-8"),
            object_pairs_hook=_manifest_object,
        )
    except OSError as exc:
        raise ValueError(f"cannot read manifest {manifest_file}: {exc}") from None
    except json.JSONDecodeError as exc:
        raise ValueError(f"invalid JSON in manifest {manifest_file}: {exc}") from None

    manifest = _require_object(manifest, "manifest")
    base_manifest_keys = {
        "files",
        "flatc",
        "flatbuffers_runtimes",
        "generator",
        "outputs",
        "payloads",
        "schema_files",
        "schema_set_sha256",
        "sdk",
    }
    optional_manifest_keys = {
        key for key in ("python_bridge", "python_package") if key in manifest
    }
    _require_keys(manifest, base_manifest_keys | optional_manifest_keys, "manifest")
    sdk = _require_object(manifest.get("sdk"), "sdk")
    _require_keys(sdk, {"name", "version"}, "sdk")
    sdk_name = _require_string(sdk.get("name"), "sdk.name")
    if _SDK_NAME_RE.fullmatch(sdk_name) is None:
        raise ValueError("sdk.name must match [a-z][a-z0-9_]*")
    sdk_version = _require_string(sdk.get("version"), "sdk.version")
    if _SEMANTIC_VERSION_RE.fullmatch(sdk_version) is None:
        raise ValueError("sdk.version must be a stable MAJOR.MINOR.PATCH semantic version")

    generator = _require_object(manifest.get("generator"), "generator")
    _require_keys(generator, {"name", "version"}, "generator")
    if generator.get("name") != "flowdata-sdk":
        raise ValueError("generator.name must be 'flowdata-sdk'")
    generator_version = _require_string(generator.get("version"), "generator.version")
    if _SEMANTIC_VERSION_RE.fullmatch(generator_version.removesuffix("-dev")) is None:
        raise ValueError("generator.version must be MAJOR.MINOR.PATCH with an optional -dev suffix")

    flatc = _require_object(manifest.get("flatc"), "flatc")
    _require_keys(
        flatc,
        {"semantic_version", "version", "version_requirement"},
        "flatc",
    )
    flatc_output = _require_string(flatc.get("version"), "flatc.version")
    expected_schema_digest = _require_sha256(
        manifest.get("schema_set_sha256"), "schema_set_sha256"
    )

    # Preserve validation precedence: schema records precede output contracts,
    # which precede payload records and generated-file integrity checks.
    schema_file_paths = verify_schema_files(manifest, schema_root, expected_schema_digest)
    sdk_kind, bridge, integrations = verify_output_contract(
        manifest, flatc, flatc_output, sdk_name, sdk_version,
    )
    verify_payloads(manifest, schema_file_paths)
    verify_output_files(manifest, manifest_path, sdk_kind, bridge, integrations, sdk_name, sdk_version)

    return manifest


def _logical_file(
    path: Path,
    sdk_root: Path,
) -> dict[str, object]:
    resolved = path.resolve()
    try:
        relative = resolved.relative_to(sdk_root.resolve())
    except ValueError:
        fail(f"generated output is outside the SDK manifest root: {resolved}")

    return {
        "path": relative.as_posix(),
        "sha256": _sha256(resolved),
        "size": resolved.stat().st_size,
    }


def write_generation_manifest(
    context: GenerationContext,
    entries: list[SchemaEntry],
    sdk_kind: str,
    integration_names: list[str],
    generated_paths: list[Path],
) -> Path:
    root = manifest_root(context, sdk_kind)
    output_path = manifest_path(context, sdk_kind)
    unique_paths = sorted({path.resolve() for path in generated_paths})
    files = [
        _logical_file(path, root)
        for path in unique_paths
        if path.resolve() != output_path.resolve()
    ]
    files.sort(key=lambda item: str(item["path"]))

    payloads = [
        {
            "file_identifier": entry.file_identifier,
            "payload_id": f"0x{entry.numeric_id:016x}",
            "qualified_root_type": entry.qualified_root_type,
            "schema": entry.schema_path.relative_to(context.schema_dir).as_posix(),
        }
        for entry in sorted(entries, key=lambda item: item.qualified_root_type)
    ]

    manifest = {
        "files": files,
        "flatc": {
            "semantic_version": str(context.flatc_version),
            "version": context.flatc_version_output,
            "version_requirement": FLATBUFFERS_SUPPORTED_VERSION_REQUIREMENT,
        },
        "flatbuffers_runtimes": flatbuffers_runtime_contracts(
            sdk_kind,
            context.cpp_python_bridge,
            context.flatc_version,
        ),
        "generator": {
            "name": "flowdata-sdk",
            "version": GENERATOR_VERSION,
        },
        "outputs": {
            "cpp_python_bridge": context.cpp_python_bridge,
            "integrations": sorted(integration_names),
            "sdk": sdk_kind,
        },
        "payloads": payloads,
        "schema_files": schema_file_records(context),
        "schema_set_sha256": schema_set_sha256(context),
        "sdk": {
            "name": context.sdk_name,
            "version": str(context.sdk_version),
        },
    }
    if sdk_kind == "python":
        manifest["python_package"] = python_package_descriptor(
            context.sdk_name,
            str(context.sdk_version),
        )
    if context.cpp_python_bridge:
        manifest["python_bridge"] = python_bridge_descriptor(context.sdk_name)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return output_path
