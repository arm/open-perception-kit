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
from dataclasses import dataclass
from pathlib import Path

from ..errors import fail
from ..types import GenerationContext, SchemaEntry


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


def _string_end(text: str, open_index: int) -> int:
    """Return the index after a quoted string, or EOF if it is unterminated."""
    i = open_index + 1
    while i < len(text):
        if text[i] == "\\":
            i += 2
        elif text[i] == '"':
            return i + 1
        else:
            i += 1
    return len(text)


def _strip_comments(text: str) -> str:
    result: list[str] = []
    i = 0
    while i < len(text):
        char = text[i]
        if char == '"':
            end = _string_end(text, i)
            result.append(text[i:end])
            i = end
            continue

        if text.startswith("//", i):
            while i < len(text) and text[i] not in "\r\n":
                i += 1
            continue

        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = end + 2 if end >= 0 else len(text)
            continue

        result.append(char)
        i += 1

    return "".join(result)


def _find_matching_brace(text: str, open_index: int) -> int:
    depth = 0
    i = open_index
    while i < len(text):
        char = text[i]
        if char == '"':
            i = _string_end(text, i)
            continue

        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1

    return -1


def _delimiter_depth(depth: int, char: str, opening: str, closing: str) -> int:
    if char == opening:
        return depth + 1
    if char == closing:
        return max(0, depth - 1)
    return depth


def _split_top_level_semicolons(text: str) -> list[str]:
    parts: list[str] = []
    start = 0
    paren_depth = 0
    bracket_depth = 0
    in_string = False

    for i, char in enumerate(text):
        # Preserve the splitter's existing quote counting, including escaped quotes.
        if char == '"':
            in_string = not in_string
            continue
        if in_string:
            continue

        paren_depth = _delimiter_depth(paren_depth, char, "(", ")")
        bracket_depth = _delimiter_depth(bracket_depth, char, "[", "]")
        if char == ";" and paren_depth == 0 and bracket_depth == 0:
            parts.append(text[start:i].strip())
            start = i + 1

    tail = text[start:].strip()
    if tail:
        parts.append(tail)
    return parts


def _schema_to_python_field_name(name: str) -> str:
    parts = name.split("_")
    if len(parts) == 1:
        return name
    return parts[0] + "".join(part[:1].upper() + part[1:] for part in parts[1:])


def _parse_type_ref(type_name: str) -> BridgeTypeRef:
    type_name = type_name.strip()
    if type_name.startswith("[") and type_name.endswith("]"):
        return BridgeTypeRef(
            kind="vector",
            name=type_name,
            element=_parse_type_ref(type_name[1:-1]),
        )
    if type_name == "string":
        return BridgeTypeRef(kind="string", name=type_name)
    if type_name in _SCALAR_TYPES:
        return BridgeTypeRef(kind=_SCALAR_TYPES[type_name], name=type_name)
    return BridgeTypeRef(kind="named", name=type_name)


def _parse_fields(body: str, schema_path: Path, namespace: str, type_name: str) -> list[BridgeField]:
    fields: list[BridgeField] = []
    for statement in _split_top_level_semicolons(body):
        if not statement or ":" not in statement:
            continue

        raw_name, raw_rest = statement.split(":", 1)
        field_name = raw_name.strip()
        if not field_name or not _CPP_IDENTIFIER_RE.fullmatch(field_name):
            fail(
                f"cannot generate C++ Python bridge for field '{field_name}' in "
                f"{schema_path}:{namespace}.{type_name}\n"
                "Fix: use field names that map directly to generated C++ object-api members."
            )

        raw_type = raw_rest.strip()
        for separator in ("=", "("):
            if separator in raw_type:
                raw_type = raw_type.split(separator, 1)[0].strip()

        if not raw_type:
            fail(
                f"cannot determine FlatBuffers field type for {namespace}.{type_name}.{field_name} "
                f"in {schema_path}"
            )

        fields.append(
            BridgeField(
                schema_name=field_name,
                py_name=_schema_to_python_field_name(field_name),
                cpp_name=field_name,
                type_ref=_parse_type_ref(raw_type),
            )
        )
    return fields


def _parse_schema_declaration(
    text: str,
    pos: int,
    token: str,
    namespace: str,
    schema_path: Path,
) -> tuple[BridgeType, int]:
    name_match = re.match(r"\s*(?a:([A-Za-z_]\w*))", text[pos:])
    if name_match is None:
        fail(f"missing {token} name in {schema_path}")
    name = name_match.group(1)
    pos += name_match.end()

    open_brace = text.find("{", pos)
    if open_brace < 0:
        fail(f"missing '{{' for {token} {namespace}.{name} in {schema_path}")
    close_brace = _find_matching_brace(text, open_brace)
    if close_brace < 0:
        fail(f"unterminated {token} {namespace}.{name} in {schema_path}")

    qualified = f"{namespace}.{name}" if namespace else name
    fields: list[BridgeField] = []
    underlying = None
    if token in {"table", "struct"}:
        fields = _parse_fields(text[open_brace + 1: close_brace], schema_path, namespace, name)
    else:
        header = text[pos:open_brace]
        underlying = "int"
        if ":" in header:
            underlying = header.split(":", 1)[1].strip().split()[0]
        if underlying not in _SCALAR_TYPES:
            fail(
                f"cannot generate C++ Python bridge for enum {qualified}: "
                f"unsupported underlying type '{underlying}'"
            )

    return BridgeType(
        kind=token,
        namespace=namespace,
        name=name,
        qualified_name=qualified,
        fields=fields,
        enum_underlying=underlying,
    ), close_brace + 1


def _parse_schema_file(schema_path: Path) -> list[BridgeType]:
    text = _strip_comments(schema_path.read_text(encoding="utf-8"))
    current_namespace = ""
    types: list[BridgeType] = []
    token_re = re.compile(r"\b(namespace|table|struct|enum)\b")
    pos = 0

    while True:
        match = token_re.search(text, pos)
        if match is None:
            break

        token = match.group(1)
        pos = match.end()

        if token == "namespace":
            semi = text.find(";", pos)
            if semi < 0:
                fail(f"unterminated namespace declaration in {schema_path}")
            current_namespace = text[pos:semi].strip()
            pos = semi + 1
            continue

        schema_type, pos = _parse_schema_declaration(text, pos, token, current_namespace, schema_path)
        types.append(schema_type)

    return types


def _resolve_named_type(name: str, namespace: str, types: dict[str, BridgeType]) -> str:
    candidates: list[str] = []
    if "." in name:
        candidates.append(name)
    else:
        if namespace:
            candidates.append(f"{namespace}.{name}")
        candidates.append(name)
        candidates.extend(
            qualified
            for qualified, candidate in types.items()
            if candidate.name == name and qualified not in candidates
        )

    matches = [candidate for candidate in candidates if candidate in types]
    if len(matches) == 1:
        return matches[0]
    if len(matches) > 1:
        fail(
            f"ambiguous FlatBuffers type reference '{name}' in namespace '{namespace}' "
            f"while generating the C++ Python bridge: {', '.join(matches)}"
        )

    fail(
        f"cannot resolve FlatBuffers type reference '{name}' in namespace '{namespace}' "
        "while generating the C++ Python bridge"
    )


def _resolve_type_ref(
    type_ref: BridgeTypeRef,
    namespace: str,
    types: dict[str, BridgeType],
) -> BridgeTypeRef:
    if type_ref.kind == "vector":
        if type_ref.element is None:
            fail("internal generator error: vector field without element type")
        return BridgeTypeRef(
            kind="vector",
            name=type_ref.name,
            element=_resolve_type_ref(type_ref.element, namespace, types),
        )

    if type_ref.kind != "named":
        return type_ref

    resolved = _resolve_named_type(type_ref.name, namespace, types)
    resolved_type = types[resolved]
    return BridgeTypeRef(kind=resolved_type.kind, name=type_ref.name, resolved=resolved)


def _schema_types(context: GenerationContext) -> dict[str, BridgeType]:
    parsed: dict[str, BridgeType] = {}
    for schema_path in context.schema_paths:
        for schema_type in _parse_schema_file(schema_path):
            if schema_type.qualified_name in parsed:
                fail(
                    f"duplicate FlatBuffers type {schema_type.qualified_name} "
                    f"while generating the C++ Python bridge"
                )
            parsed[schema_type.qualified_name] = schema_type

    resolved: dict[str, BridgeType] = {}
    for qualified_name, schema_type in parsed.items():
        resolved_fields = [
            BridgeField(
                schema_name=field.schema_name,
                py_name=field.py_name,
                cpp_name=field.cpp_name,
                type_ref=_resolve_type_ref(field.type_ref, schema_type.namespace, parsed),
            )
            for field in schema_type.fields
        ]
        resolved[qualified_name] = BridgeType(
            kind=schema_type.kind,
            namespace=schema_type.namespace,
            name=schema_type.name,
            qualified_name=qualified_name,
            fields=resolved_fields,
            enum_underlying=schema_type.enum_underlying,
        )

    return resolved


def _mangle(value: str) -> str:
    result = re.sub(r"\W", "_", value, flags=re.ASCII)
    if not result or result[0].isdigit():
        result = f"_{result}"
    return result


def _cpp_type(bridge_type: BridgeType) -> str:
    return bridge_type.cpp_type


def _python_fb_module(sdk_name: str, bridge_type: BridgeType) -> str:
    namespace = bridge_type.namespace.replace(".", ".")
    if namespace:
        return f"{sdk_name}.fb.{namespace}.{bridge_type.name}"
    return f"{sdk_name}.fb.{bridge_type.name}"


def _cpp_scalar_type(kind: str) -> str:
    return {
        "bool": "bool",
        "byte": "std::int8_t",
        "ubyte": "std::uint8_t",
        "short": "std::int16_t",
        "ushort": "std::uint16_t",
        "int": "std::int32_t",
        "uint": "std::uint32_t",
        "long": "std::int64_t",
        "ulong": "std::uint64_t",
        "float": "float",
        "double": "double",
    }[kind]


def _is_signed_integer(kind: str) -> bool:
    return kind in {"byte", "short", "int", "long"}


def _is_unsigned_integer(kind: str) -> bool:
    return kind in {"ubyte", "ushort", "uint", "ulong"}


def _ref_cpp_type(type_ref: BridgeTypeRef, types: dict[str, BridgeType]) -> str:
    if type_ref.kind in _SCALAR_TYPES.values():
        return _cpp_scalar_type(type_ref.kind)
    if type_ref.kind == "string":
        return "std::string"
    if type_ref.kind in {"table", "struct", "enum"} and type_ref.resolved is not None:
        return _cpp_type(types[type_ref.resolved])
    fail(f"internal generator error: unsupported bridge type reference {type_ref}")


def _vector_cpp_type(type_ref: BridgeTypeRef, types: dict[str, BridgeType]) -> str:
    if type_ref.kind != "vector" or type_ref.element is None:
        fail("internal generator error: vector C++ type requested for non-vector field")

    element = type_ref.element
    element_cpp = _ref_cpp_type(element, types)
    if element.kind == "table":
        return f"std::vector<std::unique_ptr<{element_cpp}>>"
    return f"std::vector<{element_cpp}>"


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
    if owner.kind == "struct":
        return f"value->{field.cpp_name}()"
    return f"value->{field.cpp_name}"


def _return_scalar_code(type_ref: BridgeTypeRef, expr: str) -> str:
    if type_ref.kind == "bool":
        return f"""if ({expr}) {{
        Py_RETURN_TRUE;
    }}
    Py_RETURN_FALSE;"""
    if _is_signed_integer(type_ref.kind):
        return f"return py_long_from_signed({expr});"
    if _is_unsigned_integer(type_ref.kind):
        return f"return py_long_from_unsigned({expr});"
    if type_ref.kind in {"float", "double"}:
        return f"return PyFloat_FromDouble(static_cast<double>({expr}));"
    if type_ref.kind == "enum":
        return f"return py_long_from_enum({expr});"
    fail(f"internal generator error: unsupported scalar bridge type {type_ref.kind}")


def _return_value_code(
    type_ref: BridgeTypeRef,
    expr: str,
    types: dict[str, BridgeType],
    anchor_expression: str,
) -> str:
    if type_ref.kind == "string":
        return f"return py_string_from_std({expr});"
    if type_ref.kind in _SCALAR_TYPES.values() or type_ref.kind == "enum":
        return _return_scalar_code(type_ref, expr)
    if type_ref.kind in {"table", "struct"} and type_ref.resolved is not None:
        nested = types[type_ref.resolved]
        make_fn = _make_proxy_function_name(nested)
        if type_ref.kind == "table":
            return f"""const auto* nested = {expr}.get();
    if (nested == nullptr) {{
        Py_RETURN_NONE;
    }}
    return {make_fn}(nested, {anchor_expression});"""
        return f"""const auto* nested = {expr}.get();
    if (nested == nullptr) {{
        Py_RETURN_NONE;
    }}
    return {make_fn}(nested, {anchor_expression});"""
    fail(f"cannot generate C++ Python bridge getter for unsupported field type {type_ref.kind}")


def _return_vector_item_code(
    type_ref: BridgeTypeRef,
    types: dict[str, BridgeType],
    anchor_expression: str,
) -> str:
    if type_ref.element is None:
        fail("internal generator error: vector item requested without element type")
    element = type_ref.element

    if element.kind == "table" and element.resolved is not None:
        nested = types[element.resolved]
        return f"""const auto& item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {{
        Py_RETURN_NONE;
    }}
    return {_make_proxy_function_name(nested)}(item.get(), {anchor_expression});"""

    if element.kind == "struct" and element.resolved is not None:
        nested = types[element.resolved]
        return f"""const auto& item = (*vector)[static_cast<std::size_t>(index)];
    return {_make_proxy_function_name(nested)}(std::addressof(item), {anchor_expression});"""

    return _return_value_code(
        element,
        "(*vector)[static_cast<std::size_t>(index)]",
        types,
        anchor_expression,
    )


def _generate_proxy_declarations(proxy_types: list[BridgeType], vector_fields: list[tuple[BridgeType, BridgeField]]) -> str:
    lines: list[str] = []
    for bridge_type in proxy_types:
        lines.append(f"PyTypeObject& {_type_object_name(bridge_type)}() {{")
        lines.append("    static PyTypeObject type = make_py_type_object();")
        lines.append("    return type;")
        lines.append("}")
        lines.append(
            f"PyObject* {_make_proxy_function_name(bridge_type)}("
            f"const {_cpp_type(bridge_type)}* value, "
            "std::shared_ptr<const native_proxy_anchor> anchor);"
        )
        lines.append("")

    for owner, field in vector_fields:
        name = _vector_name(owner, field)
        lines.append(f"PySequenceMethods& seq_{name}() {{")
        lines.append("    static PySequenceMethods methods = {};")
        lines.append("    return methods;")
        lines.append("}")
        lines.append(f"PyTypeObject& {_vector_type_object_name(owner, field)}() {{")
        lines.append("    static PyTypeObject type = make_py_type_object();")
        lines.append("    return type;")
        lines.append("}")
        lines.append("")

    return "\n".join(lines).rstrip()


def _generate_field_getter(owner: BridgeType, field: BridgeField, types: dict[str, BridgeType]) -> str:
    cpp_type = _cpp_type(owner)
    access = _field_access(owner, field)
    anchor_expression = f"proxy_anchor<{cpp_type}>(self)"
    if field.type_ref.kind == "vector":
        vector_type = _vector_cpp_type(field.type_ref, types)
        body = (
            f"return make_native_vector_proxy<{vector_type}>("
            f"{_vector_type_object_expression(owner, field)}, "
            f"std::addressof({access}), {anchor_expression});"
        )
    else:
        body = _return_value_code(field.type_ref, access, types, anchor_expression)

    return f"""PyObject* {_field_getter_name(owner, field)}(PyObject* self, void*) {{
    const auto* proxy = reinterpret_cast<const native_proxy_object<{cpp_type}>*>(self);
    const auto* value = proxy->value;
    if (value == nullptr) {{
        PyErr_SetString(PyExc_RuntimeError, "__SDK_NAME__ payload proxy is invalid");
        return nullptr;
    }}
    {body}
}}"""


def _generate_vector_proxy(owner: BridgeType, field: BridgeField, types: dict[str, BridgeType]) -> str:
    vector_type = _vector_cpp_type(field.type_ref, types)
    name = _vector_name(owner, field)
    len_fn = f"len_{name}"
    item_fn = f"item_{name}"
    ensure_fn = _ensure_vector_function_name(owner, field)
    type_obj = _vector_type_object_expression(owner, field)
    item_body = _return_vector_item_code(
        field.type_ref, types, f"proxy_anchor<{vector_type}>(self)"
    )

    return f"""Py_ssize_t {len_fn}(PyObject* self) {{
    const auto* proxy = reinterpret_cast<const native_proxy_object<{vector_type}>*>(self);
    const auto* vector = proxy->value;
    if (vector == nullptr) {{
        return 0;
    }}
    return static_cast<Py_ssize_t>(vector->size());
}}

PyObject* {item_fn}(PyObject* self, Py_ssize_t index) {{
    const auto* proxy = reinterpret_cast<const native_proxy_object<{vector_type}>*>(self);
    const auto* vector = proxy->value;
    if (vector == nullptr) {{
        PyErr_SetString(PyExc_RuntimeError, "__SDK_NAME__ vector proxy is invalid");
        return nullptr;
    }}
    if (index < 0 || static_cast<std::size_t>(index) >= vector->size()) {{
        PyErr_SetString(PyExc_IndexError, "__SDK_NAME__ vector proxy index out of range");
        return nullptr;
    }}
    {item_body}
}}

bool {ensure_fn}() {{
    auto& type = {type_obj};
    auto& sequence = seq_{name}();
    if (type.tp_name == nullptr) {{
        sequence.sq_length = {len_fn};
        sequence.sq_item = {item_fn};
        type.tp_name = "__MODULE_NAME__.{name}";
        type.tp_basicsize = sizeof(native_proxy_object<{vector_type}>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<{vector_type}>;
        type.tp_as_sequence = &sequence;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "__SDK_NAME__ read-only live vector proxy";
        type.tp_new = nullptr;
    }}

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {{
        return true;
    }}

    return PyType_Ready(&type) >= 0;
}}"""


def _generate_type_proxy(bridge_type: BridgeType, types: dict[str, BridgeType]) -> str:
    cpp_type = _cpp_type(bridge_type)
    type_obj = _type_object_expression(bridge_type)
    ensure_fn = _ensure_type_function_name(bridge_type)
    make_fn = _make_proxy_function_name(bridge_type)
    getset_name = f"getsets_{_mangle(bridge_type.qualified_name)}"

    getters = "\n\n".join(_generate_field_getter(bridge_type, field, types) for field in bridge_type.fields)
    getset_entries = "\n".join(
        f"""    {{
        "{field.py_name}",
        reinterpret_cast<getter>({_field_getter_name(bridge_type, field)}),
        nullptr,
        "read-only {field.schema_name}",
        nullptr
    }},"""
        for field in bridge_type.fields
    )

    return f"""{getters}

PyGetSetDef* {getset_name}() {{
    static PyGetSetDef definitions[] = {{
{getset_entries}
        {{nullptr, nullptr, nullptr, nullptr, nullptr}},
    }};
    return definitions;
}}

bool {ensure_fn}() {{
    auto& type = {type_obj};
    if (type.tp_name == nullptr) {{
        type.tp_name = "__MODULE_NAME__.{_mangle(bridge_type.qualified_name)}";
        type.tp_basicsize = sizeof(native_proxy_object<{cpp_type}>);
        type.tp_itemsize = 0;
        type.tp_dealloc = native_proxy_dealloc<{cpp_type}>;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "__SDK_NAME__ read-only live payload proxy";
        type.tp_getset = {getset_name}();
        type.tp_new = nullptr;
    }}

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {{
        return true;
    }}

    return PyType_Ready(&type) >= 0;
}}

PyObject* {make_fn}(
    const {cpp_type}* value,
    std::shared_ptr<const native_proxy_anchor> anchor
) {{
    return make_native_proxy<{cpp_type}>({type_obj}, value, std::move(anchor));
}}"""


def _known_kind_name(entry: SchemaEntry) -> str:
    return f"kind_{_mangle(entry.qualified_root_type)}"


def _generate_known_payload_helpers(
    entries: list[SchemaEntry],
    root_types: dict[str, BridgeType],
) -> str:
    enum_values = "\n".join(f"    {_known_kind_name(entry)}," for entry in entries)
    type_matches = "\n".join(
        f"""    if (py_type_matches(type_object, "{_python_fb_module('__PYTHON_PACKAGE_NAME__', root_types[entry.qualified_root_type])}", "{root_types[entry.qualified_root_type].py_type_name}")) {{
        return known_payload_kind::{_known_kind_name(entry)};
    }}"""
        for entry in entries
    )
    value_matches = "\n".join(
        f"""    if (py_object_type_matches(value, "{_python_fb_module('__PYTHON_PACKAGE_NAME__', root_types[entry.qualified_root_type])}", "{root_types[entry.qualified_root_type].py_type_name}")) {{
        return known_payload_kind::{_known_kind_name(entry)};
    }}"""
        for entry in entries
    )

    count_cases = "\n".join(
        f"""        case known_payload_kind::{_known_kind_name(entry)}:
            return PyLong_FromSize_t(live->envelope->count<{entry.native_type}>());"""
        for entry in entries
    )
    contains_cases = "\n".join(
        f"""        case known_payload_kind::{_known_kind_name(entry)}:
            if (live->envelope->contains<{entry.native_type}>()) {{
                Py_RETURN_TRUE;
            }}
            Py_RETURN_FALSE;"""
        for entry in entries
    )
    get_cases = "\n".join(
        f"""        case known_payload_kind::{_known_kind_name(entry)}:
            return get_known_payload<{entry.native_type}>(*live->envelope, {_type_object_expression(root_types[entry.qualified_root_type])}, index);"""
        for entry in entries
    )
    foreach_cases = "\n".join(
        f"""        case known_payload_kind::{_known_kind_name(entry)}:
            return list_known_payloads<{entry.native_type}>(*live->envelope, {_type_object_expression(root_types[entry.qualified_root_type])});"""
        for entry in entries
    )
    add_cases = "\n".join(
        f"""        case known_payload_kind::{_known_kind_name(entry)}:
            return add_known_payload<{entry.native_type}>(*live->envelope, value, "{entry.file_identifier}");"""
        for entry in entries
    )

    return f"""enum class known_payload_kind {{
{enum_values}
}};

class py_object_handle {{
public:
    explicit py_object_handle(PyObject* object = nullptr) noexcept
        : object_(object) {{
    }}

    py_object_handle(const py_object_handle&) = delete;
    py_object_handle& operator=(const py_object_handle&) = delete;

    py_object_handle(py_object_handle&& other) noexcept
        : object_(std::exchange(other.object_, nullptr)) {{
    }}

    py_object_handle& operator=(py_object_handle&& other) noexcept {{
        if (this != &other) {{
            Py_XDECREF(object_);
            object_ = std::exchange(other.object_, nullptr);
        }}
        return *this;
    }}

    ~py_object_handle() {{
        Py_XDECREF(object_);
    }}

    [[nodiscard]] PyObject* get() const noexcept {{
        return object_;
    }}

    [[nodiscard]] explicit operator bool() const noexcept {{
        return object_ != nullptr;
    }}

private:
    PyObject* object_;
}};

bool py_unicode_equals_ascii(PyObject* object, const char* expected) {{
    return PyUnicode_Check(object) && PyUnicode_CompareWithASCIIString(object, expected) == 0;
}}

bool py_type_matches(PyObject* type_object, const char* module_name, const char* class_name) {{
    if (type_object == nullptr || !PyType_Check(type_object)) {{
        return false;
    }}

    py_object_handle python_module(PyObject_GetAttrString(type_object, "__module__"));
    py_object_handle name(PyObject_GetAttrString(type_object, "__name__"));
    if (!python_module || !name) {{
        PyErr_Clear();
        return false;
    }}

    return py_unicode_equals_ascii(python_module.get(), module_name)
        && py_unicode_equals_ascii(name.get(), class_name);
}}

bool py_object_type_matches(PyObject* value, const char* module_name, const char* class_name) {{
    return value != nullptr
        && py_type_matches(reinterpret_cast<PyObject*>(Py_TYPE(value)), module_name, class_name);
}}

std::string py_type_description(PyObject* object) {{
    if (object == nullptr) {{
        return "<null>";
    }}

    PyObject* type_object = PyType_Check(object)
        ? object
        : reinterpret_cast<PyObject*>(Py_TYPE(object));

    py_object_handle python_module(PyObject_GetAttrString(type_object, "__module__"));
    py_object_handle name(PyObject_GetAttrString(type_object, "__name__"));
    if (!python_module || !name || !PyUnicode_Check(python_module.get()) || !PyUnicode_Check(name.get())) {{
        PyErr_Clear();
        return "<unknown>";
    }}

    const char* module_text = PyUnicode_AsUTF8(python_module.get());
    const char* name_text = PyUnicode_AsUTF8(name.get());
    if (module_text == nullptr || name_text == nullptr) {{
        PyErr_Clear();
        return "<unknown>";
    }}
    return std::string(module_text) + "." + name_text;
}}

std::optional<known_payload_kind> known_payload_kind_from_type(PyObject* type_object) {{
    if (type_object == nullptr || !PyType_Check(type_object)) {{
        PyErr_SetString(PyExc_TypeError, "__SDK_NAME__ known payload argument must be a generated Python payload type");
        return std::nullopt;
    }}

{type_matches}

    const auto message = "__SDK_NAME__ unknown known payload type: " + py_type_description(type_object);
    PyErr_SetString(PyExc_TypeError, message.c_str());
    return std::nullopt;
}}

std::optional<known_payload_kind> known_payload_kind_from_value(PyObject* value) {{
{value_matches}

    const auto message = "__SDK_NAME__ cannot add unsupported payload object: " + py_type_description(value);
    PyErr_SetString(PyExc_TypeError, message.c_str());
    return std::nullopt;
}}

template <container::native_payload T>
PyObject* proxy_from_payload_ref(container::payload_ref<T> ref, PyTypeObject& type) {{
    auto anchor = std::make_shared<payload_anchor<T>>(std::move(ref));
    const auto* value = anchor->ref.get();
    return make_native_proxy<T>(type, value, std::move(anchor));
}}

template <container::native_payload T>
PyObject* get_known_payload(container::envelope& envelope, PyTypeObject& type, std::size_t index) {{
    auto ref = envelope.get<T>(index);
    if (!ref) {{
        Py_RETURN_NONE;
    }}
    return proxy_from_payload_ref<T>(std::move(*ref), type);
}}

template <container::native_payload T>
PyObject* list_known_payloads(container::envelope& envelope, PyTypeObject& type) {{
    PyObject* result = PyList_New(0);
    if (result == nullptr) {{
        return nullptr;
    }}

    const auto count = envelope.count<T>();
    for (std::size_t index = 0; index < count; ++index) {{
        auto ref = envelope.get<T>(index);
        if (!ref) {{
            continue;
        }}
        PyObject* item = proxy_from_payload_ref<T>(std::move(*ref), type);
        if (item == nullptr) {{
            Py_DECREF(result);
            return nullptr;
        }}
        if (PyList_Append(result, item) < 0) {{
            Py_DECREF(item);
            Py_DECREF(result);
            return nullptr;
        }}
        Py_DECREF(item);
    }}

    return result;
}}

template <container::native_payload T>
std::optional<T> python_payload_to_native(PyObject* value, const char* file_identifier) {{
    py_object_handle flatbuffers_module(PyImport_ImportModule("flatbuffers"));
    if (!flatbuffers_module) {{
        return std::nullopt;
    }}

    py_object_handle builder_type(PyObject_GetAttrString(flatbuffers_module.get(), "Builder"));
    if (!builder_type) {{
        return std::nullopt;
    }}

    py_object_handle builder(PyObject_CallFunction(builder_type.get(), "i", 0));
    if (!builder) {{
        return std::nullopt;
    }}

    py_object_handle offset(PyObject_CallMethod(value, "Pack", "O", builder.get()));
    if (!offset) {{
        return std::nullopt;
    }}

    py_object_handle identifier(PyBytes_FromStringAndSize(file_identifier, 4));
    if (!identifier) {{
        return std::nullopt;
    }}

    if (py_object_handle finish_result(
            PyObject_CallMethod(builder.get(), "Finish", "OO", offset.get(), identifier.get()));
        !finish_result) {{
        return std::nullopt;
    }}

    py_object_handle output(PyObject_CallMethod(builder.get(), "Output", nullptr));
    if (!output) {{
        return std::nullopt;
    }}

    Py_buffer view{{}};
    if (PyObject_GetBuffer(output.get(), &view, PyBUF_SIMPLE) < 0) {{
        return std::nullopt;
    }}

    std::vector<std::uint8_t> blob;
    if (view.len > 0) {{
        const auto* begin = static_cast<const std::uint8_t*>(view.buf);
        blob.assign(begin, begin + static_cast<std::size_t>(view.len));
    }}
    PyBuffer_Release(&view);

    using traits = detail::native_traits<T>;
    if (flatbuffers::Verifier verifier(blob.data(), blob.size());
        !verifier.VerifyBuffer<typename traits::table_type>(traits::file_identifier())) {{
        PyErr_SetString(PyExc_ValueError, "__SDK_NAME__ payload object did not pack into the expected FlatBuffers root type");
        return std::nullopt;
    }}

    const auto* root = flatbuffers::GetRoot<typename traits::table_type>(blob.data());
    if (root == nullptr) {{
        PyErr_SetString(PyExc_ValueError, "__SDK_NAME__ payload object packed an invalid FlatBuffers root");
        return std::nullopt;
    }}

    T native{{}};
    root->UnPackTo(&native);
    return native;
}}

template <container::native_payload T>
PyObject* add_known_payload(container::envelope& envelope, PyObject* value, const char* file_identifier) {{
    auto native = python_payload_to_native<T>(value, file_identifier);
    if (!native) {{
        return nullptr;
    }}

    try {{
        envelope.add(std::move(*native));
    }} catch (const std::exception& exc) {{
        PyErr_SetString(PyExc_RuntimeError, exc.what());
        return nullptr;
    }}

    Py_RETURN_NONE;
}}

PyObject* live_envelope_count(PyObject* self, PyObject* args) {{
    PyObject* selector = nullptr;
    if (!PyArg_ParseTuple(args, "O:count", &selector)) {{
        return nullptr;
    }}
    const auto* live = require_live_envelope(self);
    if (live == nullptr) {{
        return nullptr;
    }}

    if (!PyType_Check(selector)) {{
        auto key = external_key_from_python(selector);
        if (!key) {{
            return nullptr;
        }}
        return PyLong_FromSize_t(live->envelope->count(*key));
    }}

    auto kind = known_payload_kind_from_type(selector);
    if (!kind) {{
        return nullptr;
    }}

    switch (*kind) {{
{count_cases}
    }}

    Py_UNREACHABLE();
}}

PyObject* live_envelope_contains_external(
    const container::envelope& envelope,
    PyObject* selector
) {{
    auto key = external_key_from_python(selector);
    if (!key) {{
        return nullptr;
    }}
    return PyBool_FromLong(envelope.contains(*key));
}}

PyObject* live_envelope_contains(PyObject* self, PyObject* args) {{
    PyObject* selector = nullptr;
    if (!PyArg_ParseTuple(args, "O:contains", &selector)) {{
        return nullptr;
    }}
    const auto* live = require_live_envelope(self);
    if (live == nullptr) {{
        return nullptr;
    }}

    if (!PyType_Check(selector)) {{
        return live_envelope_contains_external(*live->envelope, selector);
    }}

    auto kind = known_payload_kind_from_type(selector);
    if (!kind) {{
        return nullptr;
    }}

    switch (*kind) {{
{contains_cases}
    }}

    Py_UNREACHABLE();
}}

PyObject* live_envelope_get(PyObject* self, PyObject* args, PyObject* kwargs) {{
    static char selector_keyword[] = "selector";
    static char index_keyword[] = "index";
    static char* keywords[] = {{selector_keyword, index_keyword, nullptr}};
    PyObject* selector = nullptr;
    PyObject* index_object = nullptr;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|O:get", keywords, &selector, &index_object)) {{
        return nullptr;
    }}
    auto* live = require_live_envelope(self);
    if (live == nullptr) {{
        return nullptr;
    }}
    std::size_t index = 0;
    if (!parse_index(index_object, &index)) {{
        return nullptr;
    }}

    if (!PyType_Check(selector)) {{
        auto key = external_key_from_python(selector);
        if (!key) {{
            return nullptr;
        }}
        const auto payload = live->envelope->get(*key, index);
        if (!payload) {{
            Py_RETURN_NONE;
        }}
        return bytes_from_span(payload->bytes());
    }}

    auto kind = known_payload_kind_from_type(selector);
    if (!kind) {{
        return nullptr;
    }}

    switch (*kind) {{
{get_cases}
    }}

    Py_UNREACHABLE();
}}

PyObject* live_envelope_for_each(PyObject* self, PyObject* args) {{
    PyObject* selector = nullptr;
    if (!PyArg_ParseTuple(args, "O:for_each", &selector)) {{
        return nullptr;
    }}
    auto* live = require_live_envelope(self);
    if (live == nullptr) {{
        return nullptr;
    }}

    if (!PyType_Check(selector)) {{
        auto key = external_key_from_python(selector);
        if (!key) {{
            return nullptr;
        }}
        PyObject* result = PyList_New(0);
        if (result == nullptr) {{
            return nullptr;
        }}
        bool ok = true;
        live->envelope->for_each(*key, [&ok, result](std::span<const std::uint8_t> bytes) {{
            if (!ok) {{
                return;
            }}
            PyObject* item = bytes_from_span(bytes);
            if (item == nullptr) {{
                ok = false;
                return;
            }}
            if (PyList_Append(result, item) < 0) {{
                Py_DECREF(item);
                ok = false;
                return;
            }}
            Py_DECREF(item);
        }});
        if (!ok) {{
            Py_DECREF(result);
            return nullptr;
        }}
        return result;
    }}

    auto kind = known_payload_kind_from_type(selector);
    if (!kind) {{
        return nullptr;
    }}

    switch (*kind) {{
{foreach_cases}
    }}

    Py_UNREACHABLE();
}}

PyObject* live_envelope_add(PyObject* self, PyObject* args) {{
    PyObject* value = nullptr;
    PyObject* blob_object = nullptr;
    if (!PyArg_ParseTuple(args, "O|O:add", &value, &blob_object)) {{
        return nullptr;
    }}
    auto* live = require_live_envelope(self);
    if (live == nullptr) {{
        return nullptr;
    }}

    if (blob_object != nullptr) {{
        auto key = external_key_from_python(value);
        if (!key) {{
            return nullptr;
        }}

        Py_buffer view{{}};
        if (PyObject_GetBuffer(blob_object, &view, PyBUF_SIMPLE) < 0) {{
            return nullptr;
        }}

        if (view.len < 0) {{
            PyBuffer_Release(&view);
            PyErr_SetString(PyExc_ValueError, "external payload byte buffer has invalid length");
            return nullptr;
        }}

        live->envelope->add(
            *key,
            std::span<const std::uint8_t>(
                static_cast<const std::uint8_t*>(view.buf),
                static_cast<std::size_t>(view.len)
            )
        );
        PyBuffer_Release(&view);
        Py_RETURN_NONE;
    }}

    auto is_key = is_external_key_python(value);
    if (!is_key.has_value()) {{
        return nullptr;
    }}
    if (is_key.value()) {{
        PyErr_SetString(PyExc_TypeError, "__SDK_NAME__ external add requires a bytes-like blob");
        return nullptr;
    }}

    auto kind = known_payload_kind_from_value(value);
    if (!kind) {{
        return nullptr;
    }}

    switch (*kind) {{
{add_cases}
    }}

    Py_UNREACHABLE();
}}"""


def _generate_proxy_definitions(types: dict[str, BridgeType], entries: list[SchemaEntry]) -> str:
    del entries
    proxy_types = [schema_type for schema_type in types.values() if schema_type.kind in {"table", "struct"}]
    vector_fields = [
        (schema_type, field)
        for schema_type in proxy_types
        for field in schema_type.fields
        if field.type_ref.kind == "vector"
    ]

    vector_defs = "\n\n".join(_generate_vector_proxy(owner, field, types) for owner, field in vector_fields)
    type_defs = "\n\n".join(_generate_type_proxy(schema_type, types) for schema_type in proxy_types)

    ensure_calls = "\n".join(
        f"        || !{_ensure_vector_function_name(owner, field)}()"
        for owner, field in vector_fields
    )
    ensure_calls += "\n" + "\n".join(
        f"        || !{_ensure_type_function_name(schema_type)}()"
        for schema_type in proxy_types
    )
    ensure_calls = ensure_calls.strip()
    if ensure_calls:
        ensure_body = f"""    if ({ensure_calls[3:]}) {{
        return false;
    }}
    return true;"""
    else:
        ensure_body = "    return true;"

    add_calls = "\n".join(
        f"""    if (!add_python_type(python_module, {_type_object_expression(schema_type)}, "{_mangle(schema_type.qualified_name)}")) {{
        return false;
    }}"""
        for schema_type in proxy_types
    )
    if not add_calls:
        add_calls = "    (void)python_module;"

    return f"""{vector_defs}

{type_defs}

bool ensure_known_proxy_types() {{
{ensure_body}
}}

bool add_python_type(PyObject* python_module, PyTypeObject& type, const char* name) {{
    Py_INCREF(&type);
    if (PyModule_AddObject(python_module, name, reinterpret_cast<PyObject*>(&type)) < 0) {{
        Py_DECREF(&type);
        return false;
    }}
    return true;
}}

bool add_known_proxy_types(PyObject* python_module) {{
{add_calls}
    return true;
}}"""


HEADER_TEMPLATE = """// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.

#pragma once

#include <Python.h>

#include "__SDK_HEADER__"

namespace __SDK_NAME__::python_bridge {

void append_inittab();
[[nodiscard]] bool initialize_module();

[[nodiscard]] PyObject* wrap(container::envelope& envelope);
bool invalidate(PyObject* object) noexcept;

class scoped_envelope {
public:
    explicit scoped_envelope(container::envelope& envelope);
    scoped_envelope(const scoped_envelope&) = delete;
    scoped_envelope& operator=(const scoped_envelope&) = delete;
    scoped_envelope(scoped_envelope&& other) noexcept;
    scoped_envelope& operator=(scoped_envelope&& other) noexcept;
    ~scoped_envelope();

    [[nodiscard]] PyObject* py_object() const noexcept;

private:
    PyObject* object_ = nullptr;
};

} // namespace __SDK_NAME__::python_bridge
"""

SOURCE_TEMPLATE = """// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.

#include "__BRIDGE_HEADER__"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace __SDK_NAME__::python_bridge {

struct external_key_access {
    static container::external_key_t from_value(detail::id_t value) noexcept {
        return container::external_key_t(value);
    }
};

namespace {

struct live_envelope_object {
    PyObject_HEAD
    container::envelope* envelope;
    bool valid;
};

live_envelope_object* as_live_envelope(PyObject* object) noexcept;

struct native_proxy_anchor {
    virtual ~native_proxy_anchor() = default;
};

template <typename T>
struct payload_anchor final : native_proxy_anchor {
    explicit payload_anchor(container::payload_ref<T> ref_in)
        : ref(std::move(ref_in)) {
    }

    container::payload_ref<T> ref;
};

using native_proxy_anchor_ptr = std::shared_ptr<const native_proxy_anchor>;

template <typename T>
struct native_proxy_object {
    PyObject_HEAD
    const T* value;
    alignas(native_proxy_anchor_ptr)
        std::array<std::byte, sizeof(native_proxy_anchor_ptr)> anchor_storage;
};

template <typename T>
native_proxy_anchor_ptr* proxy_anchor_storage(native_proxy_object<T>* proxy) noexcept {
    return reinterpret_cast<native_proxy_anchor_ptr*>(proxy->anchor_storage.data());
}

template <typename T>
const native_proxy_anchor_ptr* proxy_anchor_storage(
    const native_proxy_object<T>* proxy
) noexcept {
    return reinterpret_cast<const native_proxy_anchor_ptr*>(proxy->anchor_storage.data());
}

template <typename T>
native_proxy_anchor_ptr* proxy_anchor_ptr(native_proxy_object<T>* proxy) noexcept {
    return std::launder(proxy_anchor_storage(proxy));
}

template <typename T>
const native_proxy_anchor_ptr* proxy_anchor_ptr(const native_proxy_object<T>* proxy) noexcept {
    return std::launder(proxy_anchor_storage(proxy));
}

template <typename T>
native_proxy_anchor_ptr proxy_anchor(PyObject* self) {
    const auto* proxy = reinterpret_cast<const native_proxy_object<T>*>(self);
    return *proxy_anchor_ptr(proxy);
}

template <typename T>
void native_proxy_dealloc(PyObject* self) {
    auto* proxy = reinterpret_cast<native_proxy_object<T>*>(self);
    std::destroy_at(proxy_anchor_ptr(proxy));
    proxy->value = nullptr;
    Py_TYPE(self)->tp_free(self);
}

template <typename T>
PyObject* make_native_proxy(
    PyTypeObject& type,
    const T* value,
    std::shared_ptr<const native_proxy_anchor> anchor
) {
    using proxy_type = native_proxy_object<T>;
    auto* object = PyObject_New(proxy_type, &type);
    if (object == nullptr) {
        return nullptr;
    }
    object->value = value;
    std::construct_at(proxy_anchor_storage(object), std::move(anchor));
    return reinterpret_cast<PyObject*>(object);
}

template <typename VectorT>
PyObject* make_native_vector_proxy(
    PyTypeObject& type,
    const VectorT* value,
    std::shared_ptr<const native_proxy_anchor> anchor
) {
    return make_native_proxy<VectorT>(type, value, std::move(anchor));
}

PyTypeObject make_py_type_object() {
    struct type_head {
        PyVarObject ob_base;
    };
    type_head head = {PyVarObject_HEAD_INIT(nullptr, 0)};
    PyTypeObject type{};
    type.ob_base = head.ob_base;
    return type;
}

template <typename Function>
PyCFunction py_c_function(Function function) {
    return reinterpret_cast<PyCFunction>(reinterpret_cast<void (*)()>(function));
}

PyObject* py_string_from_std(std::string_view value) {
    return PyUnicode_FromStringAndSize(value.data(), static_cast<Py_ssize_t>(value.size()));
}

template <typename T>
PyObject* py_long_from_signed(T value) {
    return PyLong_FromLongLong(static_cast<long long>(value));
}

template <typename T>
PyObject* py_long_from_unsigned(T value) {
    return PyLong_FromUnsignedLongLong(static_cast<unsigned long long>(value));
}

template <typename T>
PyObject* py_long_from_enum(T value) {
    using underlying_type = std::underlying_type_t<T>;
    if constexpr (std::is_signed_v<underlying_type>) {
        return py_long_from_signed(static_cast<underlying_type>(value));
    } else {
        return py_long_from_unsigned(static_cast<underlying_type>(value));
    }
}

__KNOWN_PROXY_DECLS__

void live_envelope_dealloc(PyObject* self) {
    auto* live = reinterpret_cast<live_envelope_object*>(self);
    live->envelope = nullptr;
    live->valid = false;
    Py_TYPE(self)->tp_free(self);
}

PyObject* live_envelope_is_valid(PyObject* self, PyObject*) {
    if (const auto* live = reinterpret_cast<const live_envelope_object*>(self);
        live->valid && live->envelope != nullptr) {
        Py_RETURN_TRUE;
    }
    Py_RETURN_FALSE;
}

PyObject* producer_identity_status_object(container::producer_identity_status status) {
    const char* value = nullptr;
    switch (status) {
    case container::producer_identity_status::exact_match:
        value = "exact_match";
        break;
    case container::producer_identity_status::missing:
        value = "missing";
        break;
    case container::producer_identity_status::malformed:
        value = "malformed";
        break;
    case container::producer_identity_status::sdk_name_mismatch:
        value = "sdk_name_mismatch";
        break;
    case container::producer_identity_status::sdk_version_mismatch:
        value = "sdk_version_mismatch";
        break;
    case container::producer_identity_status::schema_set_mismatch:
        value = "schema_set_mismatch";
        break;
    }

    PyObject* sdk_module = PyImport_ImportModule("__PYTHON_PACKAGE_NAME__.sdk");
    if (sdk_module == nullptr) {
        return nullptr;
    }
    PyObject* enum_type = PyObject_GetAttrString(sdk_module, "ProducerIdentityStatus");
    Py_DECREF(sdk_module);
    if (enum_type == nullptr) {
        return nullptr;
    }
    PyObject* argument = PyUnicode_FromString(value);
    if (argument == nullptr) {
        Py_DECREF(enum_type);
        return nullptr;
    }
    PyObject* result = PyObject_CallOneArg(enum_type, argument);
    Py_DECREF(argument);
    Py_DECREF(enum_type);
    return result;
}

live_envelope_object* require_live_envelope(PyObject* self) {
    auto* live = as_live_envelope(self);
    if (live == nullptr) {
        PyErr_SetString(PyExc_TypeError, "expected __MODULE_NAME__.Envelope");
        return nullptr;
    }
    if (!live->valid || live->envelope == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "__SDK_NAME__ live envelope wrapper is no longer valid");
        return nullptr;
    }
    return live;
}

PyObject* live_envelope_producer_sdk_name(PyObject* self, void*) {
    const auto* live = require_live_envelope(self);
    if (live == nullptr) {
        return nullptr;
    }
    return py_string_from_std(live->envelope->producer_sdk_name());
}

PyObject* live_envelope_producer_sdk_version(PyObject* self, void*) {
    const auto* live = require_live_envelope(self);
    if (live == nullptr) {
        return nullptr;
    }
    return py_string_from_std(live->envelope->producer_sdk_version());
}

PyObject* live_envelope_producer_schema_set_sha256(PyObject* self, void*) {
    const auto* live = require_live_envelope(self);
    if (live == nullptr) {
        return nullptr;
    }
    return py_string_from_std(live->envelope->producer_schema_set_sha256());
}

PyObject* live_envelope_producer_identity(PyObject* self, PyObject*) {
    const auto* live = require_live_envelope(self);
    if (live == nullptr) {
        return nullptr;
    }
    return producer_identity_status_object(live->envelope->producer_identity());
}

std::optional<bool> is_external_key_python(PyObject* object) {
    if (PyUnicode_Check(object) || PyLong_Check(object)) {
        return false;
    }

    PyObject* sdk_module = PyImport_ImportModule("__PYTHON_PACKAGE_NAME__");
    if (sdk_module == nullptr) {
        PyErr_SetString(PyExc_TypeError, "__SDK_NAME__ ExternalKey requires the generated Python SDK to be importable");
        return std::nullopt;
    }

    PyObject* external_key_type = PyObject_GetAttrString(sdk_module, "ExternalKey");
    Py_DECREF(sdk_module);
    if (external_key_type == nullptr) {
        PyErr_SetString(PyExc_TypeError, "__SDK_NAME__ Python SDK does not expose ExternalKey");
        return std::nullopt;
    }

    const int is_instance = PyObject_IsInstance(object, external_key_type);
    Py_DECREF(external_key_type);
    if (is_instance < 0) {
        return std::nullopt;
    }
    return is_instance != 0;
}

std::optional<container::external_key_t> external_key_from_python(PyObject* object) {
    if (PyUnicode_Check(object)) {
        PyErr_SetString(PyExc_TypeError, "__SDK_NAME__ external envelope operations require ExternalKey; call external_key(...) first");
        return std::nullopt;
    }

    if (PyLong_Check(object)) {
        PyErr_SetString(PyExc_TypeError, "__SDK_NAME__ external key must be an ExternalKey, not int");
        return std::nullopt;
    }

    auto is_key = is_external_key_python(object);
    if (!is_key.has_value()) {
        return std::nullopt;
    }
    if (!is_key.value()) {
        PyErr_SetString(PyExc_TypeError, "__SDK_NAME__ external key must be an ExternalKey");
        return std::nullopt;
    }

    PyObject* value_object = PyObject_GetAttrString(object, "value");
    if (value_object == nullptr) {
        return std::nullopt;
    }

    const auto value = PyLong_AsUnsignedLongLong(value_object);
    Py_DECREF(value_object);
    if (PyErr_Occurred()) {
        return std::nullopt;
    }

    if (value < container::external_key_min) {
        PyErr_SetString(PyExc_TypeError, "__SDK_NAME__ ExternalKey value is outside the external key domain");
        return std::nullopt;
    }

    return external_key_access::from_value(static_cast<detail::id_t>(value));
}

bool parse_index(PyObject* index_object, std::size_t* index) {
    if (index_object == nullptr) {
        *index = 0;
        return true;
    }
    if (!PyLong_Check(index_object)) {
        PyErr_SetString(PyExc_TypeError, "index must be an integer");
        return false;
    }
    const auto value = PyLong_AsSsize_t(index_object);
    if (PyErr_Occurred()) {
        return false;
    }
    if (value < 0) {
        PyErr_SetString(PyExc_ValueError, "index must be non-negative");
        return false;
    }
    *index = static_cast<std::size_t>(value);
    return true;
}

PyObject* bytes_from_span(std::span<const std::uint8_t> bytes) {
    return PyBytes_FromStringAndSize(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<Py_ssize_t>(bytes.size())
    );
}

__KNOWN_PAYLOAD_HELPERS__

__KNOWN_PROXY_DEFS__

PyMethodDef* live_envelope_methods() {
    static PyMethodDef methods[] = {
        {
        "valid",
        py_c_function(live_envelope_is_valid),
        METH_NOARGS,
        "Return True while this live envelope wrapper is valid."
    },
    {
        "producer_identity",
        py_c_function(live_envelope_producer_identity),
        METH_NOARGS,
        "Return the producer identity comparison status."
    },
    {
        "count",
        py_c_function(live_envelope_count),
        METH_VARARGS,
        "Count known payloads by generated Python payload type or external bytes by ExternalKey."
    },
    {
        "contains",
        py_c_function(live_envelope_contains),
        METH_VARARGS,
        "Return True if a known payload or external payload exists for the selector."
    },
    {
        "get",
        py_c_function(live_envelope_get),
        METH_VARARGS | METH_KEYWORDS,
        "Return a read-only live proxy for a known payload or bytes for an ExternalKey."
    },
    {
        "for_each",
        py_c_function(live_envelope_for_each),
        METH_VARARGS,
        "Return read-only live proxies or external bytes for all entries matching the selector."
    },
    {
        "add",
        py_c_function(live_envelope_add),
        METH_VARARGS,
        "Append a known payload value or external bytes with an ExternalKey."
    },
        {nullptr, nullptr, 0, nullptr},
    };
    return methods;
}

PyGetSetDef* live_envelope_getset() {
    static PyGetSetDef definitions[] = {
        {
        "producer_sdk_name",
        live_envelope_producer_sdk_name,
        nullptr,
        "SDK name recorded in the input envelope.",
        nullptr,
    },
    {
        "producer_sdk_version",
        live_envelope_producer_sdk_version,
        nullptr,
        "SDK version recorded in the input envelope.",
        nullptr,
    },
    {
        "producer_schema_set_sha256",
        live_envelope_producer_schema_set_sha256,
        nullptr,
        "Schema-set digest recorded in the input envelope.",
        nullptr,
    },
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    };
    return definitions;
}

PyTypeObject& live_envelope_type() {
    static PyTypeObject type = make_py_type_object();
    return type;
}

bool ensure_live_envelope_type() {
    auto& type = live_envelope_type();
    if (type.tp_name == nullptr) {
        type.tp_name = "__MODULE_NAME__.Envelope";
        type.tp_basicsize = sizeof(live_envelope_object);
        type.tp_itemsize = 0;
        type.tp_dealloc = live_envelope_dealloc;
        type.tp_flags = Py_TPFLAGS_DEFAULT;
        type.tp_doc = "__SDK_NAME__ live C++ envelope wrapper";
        type.tp_methods = live_envelope_methods();
        type.tp_getset = live_envelope_getset();
        type.tp_new = nullptr;
    }

    if ((type.tp_flags & Py_TPFLAGS_READY) != 0) {
        return true;
    }

    return PyType_Ready(&type) >= 0;
}

live_envelope_object* as_live_envelope(PyObject* object) noexcept {
    if (object == nullptr || !PyObject_TypeCheck(object, &live_envelope_type())) {
        return nullptr;
    }
    return reinterpret_cast<live_envelope_object*>(object);
}

PyModuleDef& bridge_module() {
    static PyModuleDef definition = {
        PyModuleDef_HEAD_INIT,
        "__MODULE_NAME__",
        "__SDK_NAME__ embedded Python bridge for live C++ envelopes",
        -1,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
    };
    return definition;
}

PyObject* init_module_impl() {
    if (!ensure_live_envelope_type()) {
        return nullptr;
    }

    if (!ensure_known_proxy_types()) {
        return nullptr;
    }

    PyObject* python_module = PyModule_Create(&bridge_module());
    if (python_module == nullptr) {
        return nullptr;
    }

    PyObject* bridge_error =
        PyErr_NewException("__MODULE_NAME__.BridgeError", PyExc_RuntimeError, nullptr);
    if (bridge_error == nullptr) {
        Py_DECREF(python_module);
        return nullptr;
    }
    if (PyModule_AddObject(python_module, "BridgeError", bridge_error) < 0) {
        Py_DECREF(bridge_error);
        Py_DECREF(python_module);
        return nullptr;
    }

    auto& envelope_type = live_envelope_type();
    Py_INCREF(&envelope_type);
    if (PyModule_AddObject(
            python_module, "Envelope", reinterpret_cast<PyObject*>(&envelope_type)) < 0) {
        Py_DECREF(&envelope_type);
        Py_DECREF(python_module);
        return nullptr;
    }

    if (!add_known_proxy_types(python_module)) {
        Py_DECREF(python_module);
        return nullptr;
    }

    return python_module;
}

} // namespace
} // namespace __SDK_NAME__::python_bridge

extern "C" PyObject* PyInit___MODULE_NAME__() {
    return __SDK_NAME__::python_bridge::init_module_impl();
}

namespace __SDK_NAME__::python_bridge {

void append_inittab() {
    if (PyImport_AppendInittab("__MODULE_NAME__", &PyInit___MODULE_NAME__) != 0) {
        throw std::runtime_error("failed to register __MODULE_NAME__ Python bridge module");
    }
}

bool initialize_module() {
    if (!Py_IsInitialized()) {
        return false;
    }

    PyObject* modules = PyImport_GetModuleDict();
    if (PyDict_GetItemString(modules, "__MODULE_NAME__") != nullptr) {
        return true;
    }

    PyObject* module = init_module_impl();
    if (module == nullptr) {
        return false;
    }
    const int result = PyDict_SetItemString(modules, "__MODULE_NAME__", module);
    Py_DECREF(module);
    return result == 0;
}

PyObject* wrap(container::envelope& envelope) {
    if (!Py_IsInitialized()) {
        return nullptr;
    }

    if (!ensure_live_envelope_type()) {
        return nullptr;
    }

    if (!ensure_known_proxy_types()) {
        return nullptr;
    }

    auto* object = PyObject_New(live_envelope_object, &live_envelope_type());
    if (object == nullptr) {
        return nullptr;
    }

    object->envelope = &envelope;
    object->valid = true;
    return reinterpret_cast<PyObject*>(object);
}

bool invalidate(PyObject* object) noexcept {
    auto* live = as_live_envelope(object);
    if (live == nullptr) {
        return false;
    }

    live->envelope = nullptr;
    live->valid = false;
    return true;
}

scoped_envelope::scoped_envelope(container::envelope& envelope)
    : object_(wrap(envelope)) {
    if (object_ == nullptr) {
        throw std::runtime_error("failed to create __MODULE_NAME__ live envelope wrapper");
    }
}

scoped_envelope::scoped_envelope(scoped_envelope&& other) noexcept
    : object_(std::exchange(other.object_, nullptr)) {
}

scoped_envelope& scoped_envelope::operator=(scoped_envelope&& other) noexcept {
    if (this != &other) {
        if (object_ != nullptr) {
            if (!Py_IsInitialized()) {
                object_ = nullptr;
            } else {
            const auto gil = PyGILState_Ensure();
            invalidate(object_);
            Py_DECREF(object_);
            PyGILState_Release(gil);
            }
        }
        object_ = std::exchange(other.object_, nullptr);
    }
    return *this;
}

scoped_envelope::~scoped_envelope() {
    if (object_ == nullptr) {
        return;
    }

    if (!Py_IsInitialized()) {
        object_ = nullptr;
        return;
    }

    const auto gil = PyGILState_Ensure();
    invalidate(object_);
    Py_DECREF(object_);
    PyGILState_Release(gil);
}

PyObject* scoped_envelope::py_object() const noexcept {
    return object_;
}

} // namespace __SDK_NAME__::python_bridge
"""


def _render(text: str, context: GenerationContext, replacements: dict[str, str] | None = None) -> str:
    module_name = f"{context.effective_public_name}_bridge"
    result = text
    for key, value in (replacements or {}).items():
        result = result.replace(key, value)
    result = result.replace("__SDK_NAME__", context.effective_public_name)
    result = result.replace(
        "__PYTHON_PACKAGE_NAME__", context.effective_public_name
    )
    result = result.replace("__SDK_HEADER__", f"{context.effective_public_name}.h")
    result = result.replace("__MODULE_NAME__", module_name)
    result = result.replace("__BRIDGE_HEADER__", f"{context.effective_public_name}_python_bridge.h")
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

    header_path = bridge_dir / f"{context.effective_public_name}_python_bridge.h"
    source_path = bridge_dir / f"{context.effective_public_name}_python_bridge.cpp"

    header_path.write_text(_render(HEADER_TEMPLATE, context), encoding="utf-8")
    source_path.write_text(_render(SOURCE_TEMPLATE, context, replacements), encoding="utf-8")

    return [header_path, source_path]
