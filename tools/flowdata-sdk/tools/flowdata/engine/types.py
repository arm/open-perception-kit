################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

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
    python_package_name: str | None = None

    @property
    def effective_python_package_name(self) -> str:
        return self.python_package_name or self.sdk_name

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
