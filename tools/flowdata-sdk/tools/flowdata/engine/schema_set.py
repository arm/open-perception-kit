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
from dataclasses import replace
from pathlib import Path

from .types import GenerationContext, RESERVED_PAYLOAD_ID_MIN, SchemaEntry


def _schema_source_hash(schema_path: Path) -> str:
    return hashlib.sha256(schema_path.read_bytes()).hexdigest()


def schema_file_records(context: GenerationContext) -> list[dict[str, object]]:
    return [
        {
            "path": schema_path.relative_to(context.schema_dir).as_posix(),
            "sha256": _schema_source_hash(schema_path),
            "size": schema_path.stat().st_size,
        }
        for schema_path in sorted(context.schema_paths)
    ]


def schema_paths_sha256(schema_dir: Path, schema_paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for schema_path in sorted(schema_paths):
        relative_path = schema_path.relative_to(schema_dir).as_posix().encode("utf-8")
        content = schema_path.read_bytes()
        digest.update(len(relative_path).to_bytes(8, "big"))
        digest.update(relative_path)
        digest.update(len(content).to_bytes(8, "big"))
        digest.update(content)
    return digest.hexdigest()


def schema_set_sha256(context: GenerationContext) -> str:
    return schema_paths_sha256(context.schema_dir, context.schema_paths)


def _generated_payload_id(entry: SchemaEntry) -> int:
    digest = hashlib.sha256(entry.qualified_root_type.encode("utf-8")).digest()
    return (int.from_bytes(digest[:8], "big") % (RESERVED_PAYLOAD_ID_MIN - 1)) + 1


def assign_payload_ids(entries: list[SchemaEntry]) -> list[SchemaEntry]:
    return [
        replace(entry, numeric_id=_generated_payload_id(entry))
        for entry in entries
    ]
