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

from dataclasses import dataclass
from pathlib import Path


RESERVED_PAYLOAD_ID_MIN = 0x8000000000000000


@dataclass(frozen=True)
class SemanticVersion:
    major: int
    minor: int
    patch: int

    def __str__(self) -> str:
        return f"{self.major}.{self.minor}.{self.patch}"


@dataclass(frozen=True)
class SchemaEntry:
    name: str
    namespace: str
    root_type_name: str
    qualified_root_type: str
    table_type: str
    native_type: str
    create_fn: str
    schema_path: Path
    schema_stem: str
    numeric_id: int
    file_identifier: str

    @property
    def generated_header_filename(self) -> str:
        return f"{self.schema_stem}_generated.h"

    @property
    def generated_header_path(self) -> Path:
        return Path("fb") / self.generated_header_filename


@dataclass(frozen=True)
class GenerationContext:
    sdk_name: str
    sdk_version: SemanticVersion
    schema_dir: Path
    schema_paths: list[Path]
    generated_root: Path
    entrypoint_path: Path
    flatc_bin: str
    flatc_version: SemanticVersion
    flatc_version_output: str
    tool_sources: list[Path]
    cpp_python_bridge: bool = False
    public_name: str | None = None

    @property
    def effective_public_name(self) -> str:
        return self.public_name or self.sdk_name

    @property
    def cpp_root(self) -> Path:
        return self.generated_root / "cpp"

    @property
    def python_root(self) -> Path:
        return self.generated_root / "python"

    @property
    def typescript_root(self) -> Path:
        return self.generated_root / "ts"

    @property
    def rust_root(self) -> Path:
        return self.generated_root / "rust"
