################################################################
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
################################################################

from __future__ import annotations

import re
from pathlib import Path

from .discovery import _strip_comments
from .errors import fail
from .types import RESERVED_PAYLOAD_ID_MIN, SchemaEntry

_NAMESPACE_DECL_RE = re.compile(
    r"\bnamespace\s+"
    r"(?P<value>[A-Za-z_][A-Za-z0-9_]*(?:(?:\.|::)[A-Za-z_][A-Za-z0-9_]*)*)\s*;"
)
_PYTHON_RUNTIME_NAMESPACE_ROOTS = {
    "flatbuffers",
    "np",
    "typing",
}


def _canonical_namespace(namespace: str) -> str:
    return namespace.replace("::", ".")


def validate_schema_namespaces(schema_paths: list[Path], sdk_name: str) -> None:
    reserved_internal = f"{sdk_name}.internalfb"

    for schema_path in schema_paths:
        text = _strip_comments(schema_path.read_text(encoding="utf-8"))
        for match in _NAMESPACE_DECL_RE.finditer(text):
            namespace = _canonical_namespace(match.group("value"))
            if namespace == reserved_internal or namespace.startswith(f"{reserved_internal}."):
                fail(
                    f"{schema_path}: namespace '{namespace}' is reserved for the generated "
                    f"{sdk_name} envelope internals\n"
                    f"Fix: use a user schema namespace outside '{reserved_internal}'."
                )

            namespace_root = namespace.split(".", 1)[0]
            if namespace_root in _PYTHON_RUNTIME_NAMESPACE_ROOTS:
                fail(
                    f"{schema_path}: namespace '{namespace}' conflicts with a Python runtime "
                    "module used by the generated SDK\n"
                    "Fix: choose a schema namespace whose first component is not "
                    f"'{namespace_root}'."
                )


def _validate_entry_namespace(entry: SchemaEntry, sdk_name: str) -> None:
    reserved_internal_cpp = f"{sdk_name}::internalfb"
    if entry.namespace == reserved_internal_cpp or entry.namespace.startswith(
        f"{reserved_internal_cpp}::"
    ):
        fail(
            f"{entry.schema_path}: namespace '{entry.namespace.replace('::', '.')}' is "
            f"reserved for the generated {sdk_name} envelope internals\n"
            f"Fix: use a user schema namespace outside '{sdk_name}.internalfb'."
        )


def validate_entries(entries: list[SchemaEntry], sdk_name: str) -> None:
    seen_numeric_ids: dict[int, Path] = {}
    seen_file_ids: dict[str, Path] = {}
    seen_native_types: dict[str, Path] = {}
    seen_table_types: dict[str, Path] = {}
    seen_roots: dict[str, Path] = {}

    for entry in entries:
        if entry.numeric_id >= RESERVED_PAYLOAD_ID_MIN:
            fail(
                f"{entry.schema_path}: generated payload id {entry.numeric_id} is reserved "
                "outside the known generated-id range\n"
                "Fix: report this generated-id collision; known payload ids must be below "
                f"{RESERVED_PAYLOAD_ID_MIN}."
            )

        _validate_entry_namespace(entry, sdk_name)

        other = seen_numeric_ids.get(entry.numeric_id)
        if other is not None:
            fail(
                f"generated payload id collision {entry.numeric_id}: {other} and {entry.schema_path}\n"
                "Fix: report this collision; known payload ids are generated from each "
                "payload root type and its schema dependency contents."
            )
        seen_numeric_ids[entry.numeric_id] = entry.schema_path

        other = seen_file_ids.get(entry.file_identifier)
        if other is not None:
            fail(
                f'duplicate file_identifier "{entry.file_identifier}": '
                f"{other} and {entry.schema_path}\n"
                "Fix: every known payload root schema must use a unique four-character "
                "FlatBuffers file_identifier."
            )
        seen_file_ids[entry.file_identifier] = entry.schema_path

        other = seen_roots.get(entry.qualified_root_type)
        if other is not None:
            fail(
                f"duplicate payload root type {entry.qualified_root_type}: "
                f"{other} and {entry.schema_path}\n"
                "Fix: define each known payload root type only once inside the schema directory."
            )
        seen_roots[entry.qualified_root_type] = entry.schema_path

        other = seen_native_types.get(entry.native_type)
        if other is not None:
            fail(f"duplicate native type {entry.native_type}: {other} and {entry.schema_path}")
        seen_native_types[entry.native_type] = entry.schema_path

        other = seen_table_types.get(entry.table_type)
        if other is not None:
            fail(f"duplicate table type {entry.table_type}: {other} and {entry.schema_path}")
        seen_table_types[entry.table_type] = entry.schema_path
