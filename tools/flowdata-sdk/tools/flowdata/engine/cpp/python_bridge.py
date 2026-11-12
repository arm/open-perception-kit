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

"""Stable bridge generation API and model identities.

Implementation modules are imported at call time so they can use these same
model classes without an import cycle. Forwarders preserve existing helper
imports and patch points used by consuming projects; implementation helpers
resolve their collaborators through this module as well.
"""
from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

from ..errors import fail
from ..types import GenerationContext, SchemaEntry
from .bridge_runtime_template import HEADER_TEMPLATE, SOURCE_TEMPLATE


_SCALAR_TYPES = {
    "bool": "bool",
    "byte": "byte",
    "ubyte": "ubyte",
    "short": "short",
    "ushort": "ushort",
    "int": "int",
    "uint": "uint",
    "long": "long",
    "ulong": "ulong",
    "int8": "byte",
    "uint8": "ubyte",
    "int16": "short",
    "uint16": "ushort",
    "int32": "int",
    "uint32": "uint",
    "int64": "long",
    "uint64": "ulong",
    "float": "float",
    "double": "double",
}

_CPP_IDENTIFIER_RE = re.compile(r"^[A-Za-z_]\w*$", re.ASCII)


@dataclass(frozen=True)
class BridgeTypeRef:
    kind: str
    name: str
    resolved: str | None = None
    element: "BridgeTypeRef | None" = None


@dataclass(frozen=True)
class BridgeField:
    schema_name: str
    py_name: str
    cpp_name: str
    type_ref: BridgeTypeRef
    optional: bool = False


@dataclass(frozen=True)
class BridgeType:
    kind: str
    namespace: str
    name: str
    qualified_name: str
    fields: list[BridgeField]
    enum_underlying: str | None = None

    @property
    def cpp_type(self) -> str:
        namespace = self.namespace.replace(".", "::")
        suffix = "T" if self.kind == "table" else ""
        return f"{namespace}::{self.name}{suffix}" if namespace else f"{self.name}{suffix}"

    @property
    def py_type_name(self) -> str:
        return f"{self.name}T" if self.kind == "table" else self.name


def _strip_comments(text: str) -> str:
    from . import bridge_schema
    return bridge_schema._strip_comments(text)


def _find_matching_brace(text: str, open_index: int) -> int:
    from . import bridge_schema
    return bridge_schema._find_matching_brace(text, open_index)


def _split_top_level_semicolons(text: str) -> list[str]:
    from . import bridge_schema
    return bridge_schema._split_top_level_semicolons(text)


def _schema_to_python_field_name(name: str) -> str:
    from . import bridge_schema
    return bridge_schema._schema_to_python_field_name(name)


def _parse_type_ref(type_name: str) -> BridgeTypeRef:
    from . import bridge_schema
    return bridge_schema._parse_type_ref(type_name)


def _parse_fields(body: str, schema_path: Path, namespace: str, type_name: str) -> list[BridgeField]:
    from . import bridge_schema
    return bridge_schema._parse_fields(body, schema_path, namespace, type_name)


def _parse_schema_file(schema_path: Path) -> list[BridgeType]:
    from . import bridge_schema
    return bridge_schema._parse_schema_file(schema_path)


def _resolve_named_type(name: str, namespace: str, types: dict[str, BridgeType]) -> str:
    from . import bridge_schema
    return bridge_schema._resolve_named_type(name, namespace, types)


def _resolve_type_ref(type_ref: BridgeTypeRef, namespace: str, types: dict[str, BridgeType]) -> BridgeTypeRef:
    from . import bridge_schema
    return bridge_schema._resolve_type_ref(type_ref, namespace, types)


def _schema_types(context: GenerationContext) -> dict[str, BridgeType]:
    from . import bridge_schema
    return bridge_schema._schema_types(context)


def _mangle(value: str) -> str:
    from . import bridge_values
    return bridge_values._mangle(value)


def _cpp_type(bridge_type: BridgeType) -> str:
    from . import bridge_values
    return bridge_values._cpp_type(bridge_type)


def _python_fb_module(sdk_name: str, bridge_type: BridgeType) -> str:
    from . import bridge_values
    return bridge_values._python_fb_module(sdk_name, bridge_type)


def _cpp_scalar_type(kind: str) -> str:
    from . import bridge_values
    return bridge_values._cpp_scalar_type(kind)


def _is_signed_integer(kind: str) -> bool:
    from . import bridge_values
    return bridge_values._is_signed_integer(kind)


def _is_unsigned_integer(kind: str) -> bool:
    from . import bridge_values
    return bridge_values._is_unsigned_integer(kind)


def _ref_cpp_type(type_ref: BridgeTypeRef, types: dict[str, BridgeType]) -> str:
    from . import bridge_values
    return bridge_values._ref_cpp_type(type_ref, types)


def _vector_cpp_type(type_ref: BridgeTypeRef, types: dict[str, BridgeType]) -> str:
    from . import bridge_values
    return bridge_values._vector_cpp_type(type_ref, types)


def _type_object_name(bridge_type: BridgeType) -> str:
    return f"pytype_{_mangle(bridge_type.qualified_name)}"


def _type_object_expression(bridge_type: BridgeType) -> str:
    return f"{_type_object_name(bridge_type)}()"


def _ensure_type_function_name(bridge_type: BridgeType) -> str:
    return f"ensure_{_mangle(bridge_type.qualified_name)}_type"


def _make_proxy_function_name(bridge_type: BridgeType) -> str:
    return f"make_{_mangle(bridge_type.qualified_name)}_proxy"


def _vector_name(owner: BridgeType, field: BridgeField) -> str:
    return _mangle(f"{owner.qualified_name}.{field.schema_name}.vector")


def _vector_type_object_name(owner: BridgeType, field: BridgeField) -> str:
    return f"pytype_{_vector_name(owner, field)}"


def _vector_type_object_expression(owner: BridgeType, field: BridgeField) -> str:
    return f"{_vector_type_object_name(owner, field)}()"


def _ensure_vector_function_name(owner: BridgeType, field: BridgeField) -> str:
    return f"ensure_{_vector_name(owner, field)}_type"


def _field_getter_name(owner: BridgeType, field: BridgeField) -> str:
    return f"get_{_mangle(owner.qualified_name)}_{_mangle(field.schema_name)}"


def _field_access(owner: BridgeType, field: BridgeField) -> str:
    from . import bridge_values
    return bridge_values._field_access(owner, field)


def _return_scalar_code(type_ref: BridgeTypeRef, expr: str) -> str:
    from . import bridge_values
    return bridge_values._return_scalar_code(type_ref, expr)


def _return_value_code(type_ref: BridgeTypeRef, expr: str, types: dict[str, BridgeType], anchor_expression: str) -> str:
    from . import bridge_values
    return bridge_values._return_value_code(type_ref, expr, types, anchor_expression)


def _return_vector_item_code(type_ref: BridgeTypeRef, types: dict[str, BridgeType], anchor_expression: str) -> str:
    from . import bridge_values
    return bridge_values._return_vector_item_code(type_ref, types, anchor_expression)


def _generate_proxy_declarations(proxy_types: list[BridgeType], vector_fields: list[tuple[BridgeType, BridgeField]]) -> str:
    from . import bridge_proxies
    return bridge_proxies._generate_proxy_declarations(proxy_types, vector_fields)


def _generate_field_getter(owner: BridgeType, field: BridgeField, types: dict[str, BridgeType]) -> str:
    from . import bridge_proxies
    return bridge_proxies._generate_field_getter(owner, field, types)


def _generate_vector_proxy(owner: BridgeType, field: BridgeField, types: dict[str, BridgeType]) -> str:
    from . import bridge_proxies
    return bridge_proxies._generate_vector_proxy(owner, field, types)


def _generate_type_proxy(bridge_type: BridgeType, types: dict[str, BridgeType]) -> str:
    from . import bridge_proxies
    return bridge_proxies._generate_type_proxy(bridge_type, types)


def _known_kind_name(entry: SchemaEntry) -> str:
    return f"kind_{_mangle(entry.qualified_root_type)}"


def _generate_known_payload_helpers(entries: list[SchemaEntry], root_types: dict[str, BridgeType]) -> str:
    from . import bridge_dispatch
    return bridge_dispatch._generate_known_payload_helpers(entries, root_types)


def _generate_proxy_definitions(types: dict[str, BridgeType], entries: list[SchemaEntry]) -> str:
    from . import bridge_proxies
    return bridge_proxies._generate_proxy_definitions(types, entries)


def _render(text: str, context: GenerationContext, replacements: dict[str, str] | None = None) -> str:
    module_name = f"{context.sdk_name}_bridge"
    result = text
    for key, value in (replacements or {}).items():
        result = result.replace(key, value)
    result = result.replace("__SDK_NAME__", context.sdk_name)
    result = result.replace("__SDK_HEADER__", f"{context.sdk_name}.h")
    result = result.replace("__MODULE_NAME__", module_name)
    result = result.replace("__BRIDGE_HEADER__", f"{context.sdk_name}_python_bridge.h")
    return result


def generate_python_bridge(context: GenerationContext, entries: list[SchemaEntry]) -> list[Path]:
    types = _schema_types(context)
    root_types: dict[str, BridgeType] = {}
    for entry in entries:
        try:
            root_type = types[entry.qualified_root_type]
        except KeyError:
            fail(
                f"cannot generate C++ Python bridge: root payload type "
                f"{entry.qualified_root_type} was not found in parsed schemas"
            )
        if root_type.kind != "table":
            fail(
                f"cannot generate C++ Python bridge for root payload {entry.qualified_root_type}: "
                "root payloads must be FlatBuffers tables"
            )
        root_types[entry.qualified_root_type] = root_type

    proxy_types = [schema_type for schema_type in types.values() if schema_type.kind in {"table", "struct"}]
    vector_fields = [
        (schema_type, field)
        for schema_type in proxy_types
        for field in schema_type.fields
        if field.type_ref.kind == "vector"
    ]

    replacements = {
        "__KNOWN_PROXY_DECLS__": _generate_proxy_declarations(proxy_types, vector_fields),
        "__KNOWN_PAYLOAD_HELPERS__": _generate_known_payload_helpers(entries, root_types),
        "__KNOWN_PROXY_DEFS__": _generate_proxy_definitions(types, entries),
    }

    bridge_dir = context.cpp_root / "python_bridge"
    bridge_dir.mkdir(parents=True, exist_ok=True)

    header_path = bridge_dir / f"{context.sdk_name}_python_bridge.h"
    source_path = bridge_dir / f"{context.sdk_name}_python_bridge.cpp"

    header_path.write_text(_render(HEADER_TEMPLATE, context), encoding="utf-8")
    source_path.write_text(_render(SOURCE_TEMPLATE, context, replacements), encoding="utf-8")

    return [header_path, source_path]
