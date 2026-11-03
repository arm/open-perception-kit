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

"""Manifest output/runtime and payload consistency checks.

The public manifest module owns shared validators and descriptors. Resolve them
there at call time to preserve existing patch points and error behavior.
"""
from __future__ import annotations

from typing import Any

from . import manifest as m


def verify_output_contract(
    manifest: dict[str, Any], flatc: dict[str, Any], flatc_output: str,
    sdk_name: str, sdk_version: str,
) -> tuple[str, bool, list[str]]:
    outputs = m._require_object(manifest.get("outputs"), "outputs")
    m._require_keys(outputs, {"cpp_python_bridge", "integrations", "sdk"}, "outputs")
    sdk_kind = m._require_string(outputs.get("sdk"), "outputs.sdk")
    if sdk_kind not in m._SUPPORTED_SDKS:
        raise ValueError(f"outputs.sdk must be one of: {', '.join(sorted(m._SUPPORTED_SDKS))}")
    bridge = outputs.get("cpp_python_bridge")
    if not isinstance(bridge, bool):
        raise ValueError("outputs.cpp_python_bridge must be a boolean")
    integrations = outputs.get("integrations")
    if not isinstance(integrations, list) or any(not isinstance(item, str) for item in integrations):
        raise ValueError("outputs.integrations must be a list of strings")
    if integrations != sorted(set(integrations)):
        raise ValueError("outputs.integrations must be sorted and contain no duplicates")
    unsupported_integrations = set(integrations) - m._SUPPORTED_INTEGRATIONS
    if unsupported_integrations:
        raise ValueError(
            "outputs.integrations contains unsupported values: "
            + ", ".join(sorted(unsupported_integrations))
        )
    if sdk_kind != "cpp" and (bridge or integrations):
        raise ValueError("C++ integrations and the Python bridge require outputs.sdk 'cpp'")

    flatc_semantic_version = m._require_string(
        flatc.get("semantic_version"), "flatc.semantic_version"
    )
    if m._SEMANTIC_VERSION_RE.fullmatch(flatc_semantic_version) is None:
        raise ValueError("flatc.semantic_version must be a stable MAJOR.MINOR.PATCH version")
    parsed_flatc_version = m.parse_flatbuffers_version(flatc_output)
    if str(parsed_flatc_version) != flatc_semantic_version:
        raise ValueError("flatc.semantic_version does not match flatc.version")
    if not m.is_supported_flatbuffers_version(parsed_flatc_version):
        raise ValueError(
            "flatc.semantic_version is outside supported range "
            f"{m.FLATBUFFERS_SUPPORTED_VERSION_REQUIREMENT}"
        )
    if flatc.get("version_requirement") != m.FLATBUFFERS_SUPPORTED_VERSION_REQUIREMENT:
        raise ValueError(
            "flatc.version_requirement must be "
            f"{m.FLATBUFFERS_SUPPORTED_VERSION_REQUIREMENT!r}"
        )
    expected_runtimes = m.flatbuffers_runtime_contracts(
        sdk_kind,
        bridge,
        parsed_flatc_version,
    )
    if manifest.get("flatbuffers_runtimes") != expected_runtimes:
        raise ValueError(
            "flatbuffers_runtimes does not match the selected SDK, bridge, and compiler"
        )

    expected_python_package = (
        m.python_package_descriptor(sdk_name, sdk_version)
        if sdk_kind == "python"
        else None
    )
    if manifest.get("python_package") != expected_python_package:
        raise ValueError("python_package does not match the generated Python SDK")

    expected_python_bridge = m.python_bridge_descriptor(sdk_name) if bridge else None
    if manifest.get("python_bridge") != expected_python_bridge:
        raise ValueError("python_bridge does not match the generated C++ bridge")
    return sdk_kind, bridge, integrations


def verify_payloads(manifest: dict[str, Any], schema_file_paths: set[str]) -> None:
    payloads = manifest.get("payloads")
    if not isinstance(payloads, list):
        raise ValueError("payloads must be a list")
    payload_sort_keys: list[str] = []
    payload_ids: set[str] = set()
    payload_types: set[str] = set()
    file_identifiers: set[str] = set()
    schema_paths: set[str] = set()
    for index, payload_value in enumerate(payloads):
        label = f"payloads[{index}]"
        payload = m._require_object(payload_value, label)
        m._require_keys(
            payload,
            {"file_identifier", "payload_id", "qualified_root_type", "schema"},
            label,
        )
        qualified_type = m._require_string(
            payload.get("qualified_root_type"), f"{label}.qualified_root_type"
        )
        file_identifier = m._require_string(
            payload.get("file_identifier"), f"{label}.file_identifier"
        )
        if len(file_identifier) != 4:
            raise ValueError(f"{label}.file_identifier must contain exactly four characters")
        payload_id = m._require_string(payload.get("payload_id"), f"{label}.payload_id")
        if m._PAYLOAD_ID_RE.fullmatch(payload_id) is None:
            raise ValueError(f"{label}.payload_id must match 0x followed by 16 lowercase hex digits")
        if int(payload_id, 16) >= 0x8000000000000000:
            raise ValueError(f"{label}.payload_id must use the known-payload id domain")
        schema_path = m._relative_manifest_path(payload.get("schema"), f"{label}.schema").as_posix()
        if schema_path not in schema_file_paths:
            raise ValueError(f"{label}.schema is missing from schema_files")
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
