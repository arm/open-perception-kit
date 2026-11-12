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

"""Schema-source and generated-file inventory verification for manifests."""
from __future__ import annotations

from pathlib import Path
from typing import Any

from . import manifest as m


def verify_schema_files(
    manifest: dict[str, Any], schema_root: Path | None, expected_schema_digest: str,
) -> set[str]:
    schema_file_paths: set[str] = set()
    schema_files = manifest.get("schema_files")
    if not isinstance(schema_files, list) or not schema_files:
        raise ValueError("schema_files must be a non-empty list")
    schema_sort_keys: list[str] = []
    verified_schema_paths: list[Path] = []
    resolved_schema_root = schema_root.resolve() if schema_root is not None else None
    for index, schema_value in enumerate(schema_files):
        label = f"schema_files[{index}]"
        item = m._require_object(schema_value, label)
        m._require_keys(item, {"path", "sha256", "size"}, label)
        relative_path = m._relative_manifest_path(item.get("path"), f"{label}.path")
        path_value = relative_path.as_posix()
        if path_value in schema_file_paths:
            raise ValueError(f"duplicate schema file entry: {path_value}")
        schema_file_paths.add(path_value)
        schema_sort_keys.append(path_value)
        size = item.get("size")
        if type(size) is not int or size < 0:
            raise ValueError(f"{label}.size must be a non-negative integer")
        expected_digest = m._require_sha256(item.get("sha256"), f"{label}.sha256")
        if resolved_schema_root is None:
            continue
        target = m._resolved_manifest_file(resolved_schema_root, relative_path, label)
        if target.stat().st_size != size:
            raise ValueError(f"{label}.size mismatch for {relative_path}")
        actual_digest = m._sha256(target)
        if actual_digest != expected_digest:
            raise ValueError(
                f"{label}.sha256 mismatch for {relative_path}: "
                f"expected {expected_digest}, found {actual_digest}"
            )
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
        if m.schema_paths_sha256(resolved_schema_root, verified_schema_paths) != expected_schema_digest:
            raise ValueError("schema_set_sha256 does not match the verified schema files")
    return schema_file_paths


def verify_output_files(
    manifest: dict[str, Any], manifest_path: Path, sdk_kind: str, bridge: bool,
    integrations: list[str], sdk_name: str, sdk_version: str,
) -> None:
    files = manifest.get("files")
    if not isinstance(files, list) or not files:
        raise ValueError("files must be a non-empty list")
    file_sort_keys: list[str] = []
    seen_files: set[str] = set()
    meson_output_found = False
    for index, file_value in enumerate(files):
        label = f"files[{index}]"
        item = m._require_object(file_value, label)
        m._require_keys(item, {"path", "sha256", "size"}, label)
        relative_path = m._relative_manifest_path(item.get("path"), f"{label}.path")
        file_key = relative_path.as_posix()
        if file_key in seen_files:
            raise ValueError(f"duplicate generated file entry: {relative_path}")
        seen_files.add(file_key)
        file_sort_keys.append(file_key)
        if relative_path.parts[0] == "meson":
            meson_output_found = True
        if relative_path.name == m.MANIFEST_FILENAME:
            raise ValueError(f"{label}.path must not list the manifest itself")
        target = m._resolved_manifest_file(manifest_path.parent, relative_path, label)
        size = item.get("size")
        if type(size) is not int or size < 0:
            raise ValueError(f"{label}.size must be a non-negative integer")
        actual_size = target.stat().st_size
        if size != actual_size:
            raise ValueError(f"{label}.size mismatch: expected {size}, found {actual_size}")
        expected_digest = m._require_sha256(item.get("sha256"), f"{label}.sha256")
        actual_digest = m._sha256(target)
        if expected_digest != actual_digest:
            raise ValueError(
                f"{label}.sha256 mismatch for {relative_path}: "
                f"expected {expected_digest}, found {actual_digest}"
            )
    if file_sort_keys != sorted(file_sort_keys):
        raise ValueError("files must be sorted by path")
    descriptor_files: set[str] = set()
    if sdk_kind == "python":
        package = m.python_package_descriptor(sdk_name, sdk_version)
        typing = package["typing"]
        descriptor_files.add(str(typing["marker"]))
        descriptor_files.update(str(path) for path in typing["stubs"])
    if bridge:
        bridge_metadata = m.python_bridge_descriptor(sdk_name)
        descriptor_files.add(str(bridge_metadata["header"]))
        descriptor_files.add(str(bridge_metadata["source"]))
    missing_descriptor_files = descriptor_files - seen_files
    if missing_descriptor_files:
        raise ValueError(
            "descriptor files are missing from files: " + ", ".join(sorted(missing_descriptor_files))
        )
    if "meson" in integrations and not meson_output_found:
        raise ValueError("outputs.integrations enables meson but files contain no Meson output")
    actual_files = set()
    for path in manifest_path.parent.rglob("*"):
        # rglob does not traverse directory symlinks; they must not hide content.
        if path.is_symlink() and not path.is_file():
            raise ValueError(f"unverified directory or broken symlink in SDK root: {path}")
        if path.is_file() and path != manifest_path:
            actual_files.add(path.relative_to(manifest_path.parent).as_posix())
    if actual_files != seen_files:
        raise ValueError("files do not match the generated SDK root")
