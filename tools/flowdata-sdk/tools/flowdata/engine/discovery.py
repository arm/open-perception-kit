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
from pathlib import Path

from .errors import fail
from .types import SchemaEntry

_DECL_RE = re.compile(
    r"\b(?P<kind>namespace|root_type)\s+"
    r"(?P<value>[A-Za-z_][A-Za-z0-9_]*(?:(?:\.|::)[A-Za-z_][A-Za-z0-9_]*)*)\s*;"
    r"|\b(?P<reset>namespace)\s*;"
)
_TYPE_DECL_RE = re.compile(
    _DECL_RE.pattern + r"|\b(?:table|struct)\s+(?P<type_name>[A-Za-z_][A-Za-z0-9_]*)\b"
)
_INCLUDE_RE = re.compile(r'\binclude\s+"([^"]+)"\s*;')
_NAMESPACE_RE = re.compile(r"\bnamespace\s+[A-Za-z_][A-Za-z0-9_\.]*\s*;")
_FILE_ID_RE = re.compile(r'\bfile_identifier\s+"([^"]*)"\s*;')
_PAYLOAD_SCHEMA_EXAMPLE = (
    "namespace example.perception;\n"
    "\n"
    "table Perception { }\n"
    "root_type Perception;\n"
    'file_identifier "PRCP";'
)


def _schema_error(schema_path: Path, problem: str, example: str, fix: str) -> None:
    fail(
        f"{schema_path}: {problem}\n"
        f"Example:\n{example}\n"
        f"Fix: {fix}"
    )


def _payload_name(namespace_cpp: str, root_type_name: str) -> str:
    return f"{namespace_cpp}::{root_type_name}"


def _canonical_root_type(namespace_cpp: str, root_type_name: str) -> str:
    return f"{namespace_cpp.replace('::', '.')}.{root_type_name}"


def _mask_strings(source: str) -> str:
    """Hide quoted text without changing offsets used to read the original source."""
    return re.sub(r'"(?:\\.|[^"\\])*"', lambda match: " " * len(match.group()), source, flags=re.S)


def _strip_comments(source: str) -> str:
    result: list[str] = []
    index = 0
    in_string = False
    escaped = False

    while index < len(source):
        char = source[index]
        next_char = source[index + 1] if index + 1 < len(source) else ""

        if in_string:
            result.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            index += 1
            continue

        if char == '"':
            in_string = True
            result.append(char)
            index += 1
            continue

        if char == "/" and next_char == "/":
            result.extend("  ")
            index += 2
            while index < len(source) and source[index] not in "\r\n":
                result.append(" ")
                index += 1
            continue

        if char == "/" and next_char == "*":
            result.extend("  ")
            index += 2
            while index < len(source):
                if source[index] == "*" and index + 1 < len(source) and source[index + 1] == "/":
                    result.extend("  ")
                    index += 2
                    break
                result.append("\n" if source[index] in "\r\n" else " ")
                index += 1
            continue

        result.append(char)
        index += 1

    return "".join(result)


def _schema_type_names(schema_path: Path, text: str, schema_dir: Path, seen: set[Path]) -> set[str]:
    seen.add(schema_path.resolve())
    declarations = _mask_strings(text)
    namespace = ""
    names: set[str] = set()
    for match in _TYPE_DECL_RE.finditer(declarations):
        if match.group("reset"):
            namespace = ""
        elif name := match.group("type_name"):
            names.add(f"{namespace}.{name}" if namespace else name)
        elif match.group("kind") == "namespace":
            namespace = match.group("value").replace("::", ".")
    for match in _INCLUDE_RE.finditer(text):
        if not declarations.startswith("include", match.start()):
            continue
        for directory in (schema_path.parent, schema_dir):
            included = (directory / match.group(1)).resolve()
            if included.is_file():
                if included not in seen:
                    names.update(_schema_type_names(
                        included, _strip_comments(included.read_text(encoding="utf-8")), schema_dir, seen,
                    ))
                break
    return names


def _root_type_namespace(
    schema_path: Path, text: str, schema_dir: Path | None = None,
) -> tuple[str, str] | None:
    active_namespace: str | None = None
    roots: list[tuple[str, str | None, int]] = []

    for match in _DECL_RE.finditer(_mask_strings(text)):
        if match.group("reset"):
            active_namespace = None
            continue
        kind = match.group("kind")
        value = match.group("value").replace("::", ".")
        if kind == "namespace":
            active_namespace = value
            continue

        roots.append((value, active_namespace, match.start()))

    if not roots:
        return None

    if len(roots) > 1:
        _schema_error(
            schema_path,
            f"expected zero or one root_type declaration, found {len(roots)}",
            _PAYLOAD_SCHEMA_EXAMPLE,
            "keep helper types in the schema, but declare only one payload root_type per file.",
        )

    value, namespace, root_position = roots[0]
    names = _schema_type_names(schema_path, text[:root_position], schema_dir or schema_path.parent, set())
    # Unlike field types, flatc resolves roots by exact name, then the active
    # namespace only. Later declarations cannot shadow a root already selected.
    for candidate in (value, f"{namespace}.{value}" if namespace else value):
        if candidate in names:
            value = candidate
            namespace = None  # An actual global type has no namespace, even inside a namespace block.
            break
    if "." in value:
        namespace, root_type_name = value.rsplit(".", 1)
    else:
        root_type_name = value
    if namespace is None:
        _schema_error(
            schema_path,
            "root_type has no active FlatBuffers namespace",
            _PAYLOAD_SCHEMA_EXAMPLE,
            "add a `namespace ...;` declaration before the root_type, or use a qualified root_type.",
        )

    return namespace.replace(".", "::"), root_type_name


def _parse_schema_entry(schema_path: Path, schema_dir: Path | None = None) -> SchemaEntry | None:
    text = _strip_comments(schema_path.read_text(encoding="utf-8"))
    root = _root_type_namespace(schema_path, text, schema_dir)
    if root is None:
        return None

    declarations = _mask_strings(text)
    if not _NAMESPACE_RE.search(declarations):
        _schema_error(
            schema_path,
            "payload root schema is missing a FlatBuffers namespace declaration",
            _PAYLOAD_SCHEMA_EXAMPLE,
            "add a `namespace ...;` declaration to every schema file that declares a root_type.",
        )

    namespace_cpp, root_type_name = root

    file_id_match = next((match for match in _FILE_ID_RE.finditer(text)
                          if declarations.startswith("file_identifier", match.start())), None)
    if not file_id_match:
        _schema_error(
            schema_path,
            "payload root schema is missing a FlatBuffers file_identifier declaration",
            _PAYLOAD_SCHEMA_EXAMPLE,
            "add a four-character `file_identifier \"ABCD\";` declaration to every root schema.",
        )
    file_identifier = file_id_match.group(1)
    if len(file_identifier) != 4:
        _schema_error(
            schema_path,
            f'file_identifier must be exactly 4 characters, got "{file_identifier}"',
            'file_identifier "PRCP";',
            "use exactly four ASCII characters for the payload file identifier.",
        )

    qualified_root_type = _canonical_root_type(namespace_cpp, root_type_name)

    return SchemaEntry(
        name=_payload_name(namespace_cpp, root_type_name),
        namespace=namespace_cpp,
        root_type_name=root_type_name,
        qualified_root_type=qualified_root_type,
        table_type=f"{namespace_cpp}::{root_type_name}",
        native_type=f"{namespace_cpp}::{root_type_name}T",
        create_fn=f"{namespace_cpp}::Create{root_type_name}",
        schema_path=schema_path,
        schema_stem=schema_path.stem,
        numeric_id=0,
        file_identifier=file_identifier,
    )


def discover_schema_set(schema_dir: Path) -> tuple[list[Path], list[SchemaEntry]]:
    resolved_dir = schema_dir.resolve()

    if not resolved_dir.exists():
        fail(
            f"{schema_dir}: schema directory does not exist\n"
            "Example: --schema-dir payloads\n"
            "Fix: pass an existing directory containing the complete FlatBuffers schema set."
        )

    if not resolved_dir.is_dir():
        fail(
            f"{schema_dir}: expected a schema directory, got a file\n"
            "Example: --schema-dir payloads\n"
            "Fix: pass the directory containing all .fbs files, not an individual schema file."
        )

    schema_paths = sorted(path.resolve() for path in resolved_dir.rglob("*.fbs") if path.is_file())
    if not schema_paths:
        fail(
            f"{resolved_dir}: no .fbs files found\n"
            "Example: --schema-dir payloads\n"
            "Fix: put the complete FlatBuffers schema set under this directory."
        )

    seen_basenames: dict[str, Path] = {}
    for schema_path in schema_paths:
        other = seen_basenames.get(schema_path.name)
        if other is not None:
            fail(
                f'duplicate FlatBuffers schema basename "{schema_path.name}": '
                f"{other} and {schema_path}\n"
                "Fix: use unique .fbs filenames inside the schema directory. FlatBuffers "
                "generates language files from the schema basename."
            )
        seen_basenames[schema_path.name] = schema_path

    entries = [
        entry
        for entry in (_parse_schema_entry(schema_path, resolved_dir) for schema_path in schema_paths)
        if entry is not None
    ]
    if not entries:
        fail(
            f"{resolved_dir}: no payload root schemas found\n"
            "Example:\n"
            "  namespace example.perception;\n"
            "  table Perception { }\n"
            "  root_type Perception;\n"
            '  file_identifier "PRCP";\n'
            "Fix: declare `root_type <Type>;` in every schema file that represents a known payload."
        )

    return schema_paths, entries
