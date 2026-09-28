################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

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


def python_package_descriptor(
    public_name: str,
    sdk_version: str,
) -> dict[str, Any]:
    return {
        "build_backend": PYTHON_BUILD_BACKEND,
        "distribution_name": public_name,
        "import_name": public_name,
        "pure_python": True,
        "requires_python": PYTHON_VERSION_REQUIREMENT,
        "typing": {
            "marker": f"src/{public_name}/py.typed",
            "stubs": [f"src/{public_name}/guest.pyi"],
        },
        "version": sdk_version,
        "wheel_tag": PYTHON_WHEEL_TAG,
    }


def python_bridge_descriptor(public_name: str) -> dict[str, Any]:
    return {
        "header": f"python_bridge/{public_name}_python_bridge.h",
        "initialization_function": f"{public_name}::python_bridge::initialize_module",
        "module_name": f"{public_name}_bridge",
        "registration_function": f"{public_name}::python_bridge::append_inittab",
        "requires_python": PYTHON_VERSION_REQUIREMENT,
        "sdk_import_name": public_name,
        "source": f"python_bridge/{public_name}_python_bridge.cpp",
        "wrapper_type": f"{public_name}::python_bridge::scoped_envelope",
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
    sdk_name, public_name, sdk_version = _verify_manifest_identity(manifest)

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
    schema_file_paths = _verify_schema_files(
        manifest.get("schema_files"), schema_root, expected_schema_digest
    )
    sdk_kind, bridge, integrations = _verify_outputs(manifest.get("outputs"))
    _verify_flatbuffers(manifest, flatc, flatc_output, sdk_kind, bridge)
    _verify_python_descriptors(
        manifest, public_name, sdk_version, sdk_kind, bridge
    )
    _verify_payloads(manifest.get("payloads"), schema_file_paths)
    seen_files, meson_output_found = _verify_generated_files(
        manifest.get("files"), manifest_path
    )
    _verify_descriptor_files(
        seen_files,
        sdk_version,
        sdk_kind,
        bridge,
        public_name,
    )
    _verify_sdk_root(manifest_path, seen_files, integrations, meson_output_found)
    return manifest


def _verify_manifest_identity(manifest: dict[str, Any]) -> tuple[str, str, str]:
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
    optional_sdk_keys = {"schema_namespace"} if "schema_namespace" in sdk else set()
    _require_keys(sdk, {"name", "version"} | optional_sdk_keys, "sdk")
    public_name = _require_string(sdk.get("name"), "sdk.name")
    if _SDK_NAME_RE.fullmatch(public_name) is None:
        raise ValueError("sdk.name must match [a-z][a-z0-9_]*")
    sdk_name = _require_string(sdk.get("schema_namespace", public_name), "sdk.schema_namespace")
    if _SDK_NAME_RE.fullmatch(sdk_name) is None:
        raise ValueError("sdk.schema_namespace must match [a-z][a-z0-9_]*")
    sdk_version = _require_string(sdk.get("version"), "sdk.version")
    if _SEMANTIC_VERSION_RE.fullmatch(sdk_version) is None:
        raise ValueError("sdk.version must be a stable MAJOR.MINOR.PATCH semantic version")

    generator = _require_object(manifest.get("generator"), "generator")
    _require_keys(generator, {"name", "version"}, "generator")
    if generator.get("name") != "flowdata-sdk":
        raise ValueError("generator.name must be 'flowdata-sdk'")
    generator_version = _require_string(generator.get("version"), "generator.version")
    if _SEMANTIC_VERSION_RE.fullmatch(generator_version) is None:
        raise ValueError("generator.version must be a stable MAJOR.MINOR.PATCH semantic version")
    return sdk_name, public_name, sdk_version


def _verify_schema_file(
    item: dict[str, Any],
    relative_path: PurePosixPath,
    label: str,
    resolved_schema_root: Path | None,
) -> Path | None:
    size = item.get("size")
    if type(size) is not int or size < 0:
        raise ValueError(f"{label}.size must be a non-negative integer")
    expected_digest = _require_sha256(item.get("sha256"), f"{label}.sha256")
    if resolved_schema_root is None:
        return None
    target = _resolved_manifest_file(resolved_schema_root, relative_path, label)
    if target.stat().st_size != size:
        raise ValueError(f"{label}.size mismatch for {relative_path}")
    actual_digest = _sha256(target)
    if actual_digest != expected_digest:
        raise ValueError(
            f"{label}.sha256 mismatch for {relative_path}: "
            f"expected {expected_digest}, found {actual_digest}"
        )
    return target


def _verify_schema_files(
    schema_files: Any,
    schema_root: Path | None,
    expected_schema_digest: str,
) -> set[str]:
    schema_file_paths: set[str] = set()
    if not isinstance(schema_files, list) or not schema_files:
        raise ValueError("schema_files must be a non-empty list")
    schema_sort_keys: list[str] = []
    verified_schema_paths: list[Path] = []
    resolved_schema_root = schema_root.resolve() if schema_root is not None else None
    for index, schema_value in enumerate(schema_files):
        label = f"schema_files[{index}]"
        item = _require_object(schema_value, label)
        _require_keys(item, {"path", "sha256", "size"}, label)
        relative_path = _relative_manifest_path(item.get("path"), f"{label}.path")
        path_value = relative_path.as_posix()
        if path_value in schema_file_paths:
            raise ValueError(f"duplicate schema file entry: {path_value}")
        schema_file_paths.add(path_value)
        schema_sort_keys.append(path_value)
        target = _verify_schema_file(item, relative_path, label, resolved_schema_root)
        if target is not None:
            verified_schema_paths.append(target)
    if schema_sort_keys != sorted(schema_sort_keys):
        raise ValueError("schema_files must be sorted by path")
    if resolved_schema_root is not None:
        actual_schema_paths = {
            path.relative_to(resolved_schema_root).as_posix()
            for path in resolved_schema_root.rglob("*.fbs")
            if path.is_file()
        }
        if actual_schema_paths != schema_file_paths:
            raise ValueError("schema_files do not match the schema root")
        if schema_paths_sha256(resolved_schema_root, verified_schema_paths) != expected_schema_digest:
            raise ValueError("schema_set_sha256 does not match the verified schema files")
    return schema_file_paths


def _verify_outputs(value: Any) -> tuple[str, bool, list[str]]:
    outputs = _require_object(value, "outputs")
    _require_keys(outputs, {"cpp_python_bridge", "integrations", "sdk"}, "outputs")
    sdk_kind = _require_string(outputs.get("sdk"), "outputs.sdk")
    if sdk_kind not in _SUPPORTED_SDKS:
        raise ValueError(f"outputs.sdk must be one of: {', '.join(sorted(_SUPPORTED_SDKS))}")
    bridge = outputs.get("cpp_python_bridge")
    if not isinstance(bridge, bool):
        raise ValueError("outputs.cpp_python_bridge must be a boolean")
    integrations = outputs.get("integrations")
    if not isinstance(integrations, list) or any(not isinstance(item, str) for item in integrations):
        raise ValueError("outputs.integrations must be a list of strings")
    if integrations != sorted(set(integrations)):
        raise ValueError("outputs.integrations must be sorted and contain no duplicates")
    unsupported_integrations = set(integrations) - _SUPPORTED_INTEGRATIONS
    if unsupported_integrations:
        raise ValueError(
            "outputs.integrations contains unsupported values: "
            + ", ".join(sorted(unsupported_integrations))
        )
    if sdk_kind != "cpp" and (bridge or integrations):
        raise ValueError("C++ integrations and the Python bridge require outputs.sdk 'cpp'")
    return sdk_kind, bridge, integrations


def _verify_flatbuffers(
    manifest: dict[str, Any],
    flatc: dict[str, Any],
    flatc_output: str,
    sdk_kind: str,
    bridge: bool,
) -> None:
    flatc_semantic_version = _require_string(
        flatc.get("semantic_version"), "flatc.semantic_version"
    )
    if _SEMANTIC_VERSION_RE.fullmatch(flatc_semantic_version) is None:
        raise ValueError("flatc.semantic_version must be a stable MAJOR.MINOR.PATCH version")
    parsed_flatc_version = parse_flatbuffers_version(flatc_output)
    if str(parsed_flatc_version) != flatc_semantic_version:
        raise ValueError("flatc.semantic_version does not match flatc.version")
    if not is_supported_flatbuffers_version(parsed_flatc_version):
        raise ValueError(
            "flatc.semantic_version is outside supported range "
            f"{FLATBUFFERS_SUPPORTED_VERSION_REQUIREMENT}"
        )
    if flatc.get("version_requirement") != FLATBUFFERS_SUPPORTED_VERSION_REQUIREMENT:
        raise ValueError(
            "flatc.version_requirement must be "
            f"{FLATBUFFERS_SUPPORTED_VERSION_REQUIREMENT!r}"
        )
    expected_runtimes = flatbuffers_runtime_contracts(
        sdk_kind,
        bridge,
        parsed_flatc_version,
    )
    if manifest.get("flatbuffers_runtimes") != expected_runtimes:
        raise ValueError(
            "flatbuffers_runtimes does not match the selected SDK, bridge, and compiler"
        )


def _verify_python_descriptors(
    manifest: dict[str, Any],
    public_name: str,
    sdk_version: str,
    sdk_kind: str,
    bridge: bool,
) -> None:
    expected_python_package = (
        python_package_descriptor(public_name, sdk_version)
        if sdk_kind == "python" else None
    )
    if manifest.get("python_package") != expected_python_package:
        raise ValueError("python_package does not match the generated Python SDK")

    expected_python_bridge = (
        python_bridge_descriptor(public_name) if bridge else None
    )
    if manifest.get("python_bridge") != expected_python_bridge:
        raise ValueError("python_bridge does not match the generated C++ bridge")


def _verify_payload(
    payload_value: Any,
    label: str,
    schema_file_paths: set[str],
) -> tuple[str, str, str, str]:
    payload = _require_object(payload_value, label)
    _require_keys(
        payload,
        {"file_identifier", "payload_id", "qualified_root_type", "schema"},
        label,
    )
    qualified_type = _require_string(
        payload.get("qualified_root_type"), f"{label}.qualified_root_type"
    )
    file_identifier = _require_string(
        payload.get("file_identifier"), f"{label}.file_identifier"
    )
    if len(file_identifier) != 4:
        raise ValueError(f"{label}.file_identifier must contain exactly four characters")
    payload_id = _require_string(payload.get("payload_id"), f"{label}.payload_id")
    if _PAYLOAD_ID_RE.fullmatch(payload_id) is None:
        raise ValueError(f"{label}.payload_id must match 0x followed by 16 lowercase hex digits")
    if int(payload_id, 16) >= 0x8000000000000000:
        raise ValueError(f"{label}.payload_id must use the known-payload id domain")
    schema_path = _relative_manifest_path(payload.get("schema"), f"{label}.schema").as_posix()
    if schema_path not in schema_file_paths:
        raise ValueError(f"{label}.schema is missing from schema_files")
    return payload_id, qualified_type, file_identifier, schema_path


def _verify_payloads(payloads: Any, schema_file_paths: set[str]) -> None:
    if not isinstance(payloads, list):
        raise ValueError("payloads must be a list")
    payload_sort_keys: list[str] = []
    payload_ids: set[str] = set()
    payload_types: set[str] = set()
    file_identifiers: set[str] = set()
    schema_paths: set[str] = set()
    for index, payload_value in enumerate(payloads):
        label = f"payloads[{index}]"
        payload_id, qualified_type, file_identifier, schema_path = _verify_payload(
            payload_value, label, schema_file_paths
        )
        for value, seen, field in (
            (payload_id, payload_ids, "payload_id"),
            (qualified_type, payload_types, "qualified_root_type"),
            (file_identifier, file_identifiers, "file_identifier"),
            (schema_path, schema_paths, "schema"),
        ):
            if value in seen:
                raise ValueError(f"duplicate {field} in payloads: {value}")
            seen.add(value)
        payload_sort_keys.append(qualified_type)
    if payload_sort_keys != sorted(payload_sort_keys):
        raise ValueError("payloads must be sorted by qualified_root_type")


def _verify_generated_file(
    item: dict[str, Any],
    relative_path: PurePosixPath,
    label: str,
    sdk_root: Path,
) -> None:
    if relative_path.name == MANIFEST_FILENAME:
        raise ValueError(f"{label}.path must not list the manifest itself")
    target = _resolved_manifest_file(sdk_root, relative_path, label)
    size = item.get("size")
    if type(size) is not int or size < 0:
        raise ValueError(f"{label}.size must be a non-negative integer")
    actual_size = target.stat().st_size
    if size != actual_size:
        raise ValueError(f"{label}.size mismatch: expected {size}, found {actual_size}")
    expected_digest = _require_sha256(item.get("sha256"), f"{label}.sha256")
    actual_digest = _sha256(target)
    if expected_digest != actual_digest:
        raise ValueError(
            f"{label}.sha256 mismatch for {relative_path}: "
            f"expected {expected_digest}, found {actual_digest}"
        )


def _verify_generated_files(files: Any, manifest_path: Path) -> tuple[set[str], bool]:
    if not isinstance(files, list) or not files:
        raise ValueError("files must be a non-empty list")
    file_sort_keys: list[str] = []
    seen_files: set[str] = set()
    meson_output_found = False
    for index, file_value in enumerate(files):
        label = f"files[{index}]"
        item = _require_object(file_value, label)
        _require_keys(item, {"path", "sha256", "size"}, label)
        relative_path = _relative_manifest_path(item.get("path"), f"{label}.path")
        file_key = relative_path.as_posix()
        if file_key in seen_files:
            raise ValueError(f"duplicate generated file entry: {relative_path}")
        seen_files.add(file_key)
        file_sort_keys.append(file_key)
        if relative_path.parts[0] == "meson":
            meson_output_found = True
        _verify_generated_file(item, relative_path, label, manifest_path.parent)
    if file_sort_keys != sorted(file_sort_keys):
        raise ValueError("files must be sorted by path")
    return seen_files, meson_output_found


def _verify_descriptor_files(
    seen_files: set[str],
    sdk_version: str,
    sdk_kind: str,
    bridge: bool,
    public_name: str,
) -> None:
    descriptor_files: set[str] = set()
    if sdk_kind == "python":
        package = python_package_descriptor(public_name, sdk_version)
        typing = package["typing"]
        descriptor_files.add(str(typing["marker"]))
        descriptor_files.update(str(path) for path in typing["stubs"])
    if bridge:
        bridge_metadata = python_bridge_descriptor(public_name)
        descriptor_files.add(str(bridge_metadata["header"]))
        descriptor_files.add(str(bridge_metadata["source"]))
    missing_descriptor_files = descriptor_files - seen_files
    if missing_descriptor_files:
        raise ValueError(
            "descriptor files are missing from files: " + ", ".join(sorted(missing_descriptor_files))
        )


def _verify_sdk_root(
    manifest_path: Path,
    seen_files: set[str],
    integrations: list[str],
    meson_output_found: bool,
) -> None:
    if "meson" in integrations and not meson_output_found:
        raise ValueError("outputs.integrations enables meson but files contain no Meson output")
    actual_files = {
        path.relative_to(manifest_path.parent).as_posix()
        for path in manifest_path.parent.rglob("*")
        if path.is_file() and path != manifest_path
    }
    if actual_files != seen_files:
        raise ValueError("files do not match the generated SDK root")


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
            "name": context.effective_public_name,
            "version": str(context.sdk_version),
        },
    }
    if context.effective_public_name != context.sdk_name:
        manifest["sdk"]["schema_namespace"] = context.sdk_name
    if sdk_kind == "python":
        manifest["python_package"] = python_package_descriptor(
            context.effective_public_name,
            str(context.sdk_version),
        )
    if context.cpp_python_bridge:
        manifest["python_bridge"] = python_bridge_descriptor(context.effective_public_name)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return output_path
