################################################################
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
################################################################

from __future__ import annotations

import re
from pathlib import Path

from .errors import fail
from .types import SchemaEntry

_DECL_RE = re.compile(
    r"\b(?P<kind>namespace|root_type)\s+"
    r"(?P<value>[A-Za-z_][A-Za-z0-9_]*(?:(?:\.|::)[A-Za-z_][A-Za-z0-9_]*)*)\s*;"
)
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


def _quoted_string_end(source: str, index: int) -> int:
    index += 1
    escaped = False
    while index < len(source):
        char = source[index]
        index += 1
        if escaped:
            escaped = False
        elif char == "\\":
            escaped = True
        elif char == '"':
            break
    return index


def _blank_comment(source: str, index: int) -> tuple[str, int]:
    if source.startswith("//", index):
        end = index + 2
        while end < len(source) and source[end] not in "\r\n":
            end += 1
        return " " * (end - index), end

    closing = source.find("*/", index + 2)
    end = len(source) if closing == -1 else closing + 2
    return "".join("\n" if char in "\r\n" else " " for char in source[index:end]), end


def _strip_comments(source: str) -> str:
    result: list[str] = []
    index = 0
    while index < len(source):
        char = source[index]
        if char == '"':
            end = _quoted_string_end(source, index)
            result.append(source[index:end])
            index = end
        elif source.startswith(("//", "/*"), index):
            blanked, index = _blank_comment(source, index)
            result.append(blanked)
        else:
            result.append(char)
            index += 1

    return "".join(result)


def _root_type_namespace(schema_path: Path, text: str) -> tuple[str, str] | None:
    active_namespace: str | None = None
    roots: list[tuple[str, str | None]] = []

    for match in _DECL_RE.finditer(text):
        kind = match.group("kind")
        value = match.group("value").replace("::", ".")
        if kind == "namespace":
            active_namespace = value
            continue

        if "." in value:
            namespace, root_type_name = value.rsplit(".", 1)
            roots.append((root_type_name, namespace))
        else:
            roots.append((value, active_namespace))

    if not roots:
        return None

    if len(roots) > 1:
        _schema_error(
            schema_path,
            f"expected zero or one root_type declaration, found {len(roots)}",
            _PAYLOAD_SCHEMA_EXAMPLE,
            "keep helper types in the schema, but declare only one payload root_type per file.",
        )

    root_type_name, namespace = roots[0]
    if namespace is None:
        _schema_error(
            schema_path,
            "root_type has no active FlatBuffers namespace",
            _PAYLOAD_SCHEMA_EXAMPLE,
            "add a `namespace ...;` declaration before the root_type, or use a qualified root_type.",
        )

    return namespace.replace(".", "::"), root_type_name


def _parse_schema_entry(schema_path: Path) -> SchemaEntry | None:
    text = _strip_comments(schema_path.read_text(encoding="utf-8"))
    root = _root_type_namespace(schema_path, text)
    if root is None:
        return None

    if not _NAMESPACE_RE.search(text):
        _schema_error(
            schema_path,
            "payload root schema is missing a FlatBuffers namespace declaration",
            _PAYLOAD_SCHEMA_EXAMPLE,
            "add a `namespace ...;` declaration to every schema file that declares a root_type.",
        )

    namespace_cpp, root_type_name = root

    file_id_match = _FILE_ID_RE.search(text)
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
        for entry in (_parse_schema_entry(schema_path) for schema_path in schema_paths)
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
