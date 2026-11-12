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

"""Schema-specific C++ proxy declarations, getters and registration fragments."""
from __future__ import annotations

from ..types import SchemaEntry
from . import python_bridge as bridge
from .python_bridge import BridgeField, BridgeType


def _generate_proxy_declarations(proxy_types: list[BridgeType], vector_fields: list[tuple[BridgeType, BridgeField]]) -> str:
    lines: list[str] = []
    for bridge_type in proxy_types:
        lines.append(f"PyTypeObject& {bridge._type_object_name(bridge_type)}() {{")
        lines.append("    static PyTypeObject type = make_py_type_object();")
        lines.append("    return type;")
        lines.append("}")
        lines.append(
            f"PyObject* {bridge._make_proxy_function_name(bridge_type)}("
            f"const {bridge._cpp_type(bridge_type)}* value, "
            "std::shared_ptr<const native_proxy_anchor> anchor);"
        )
        lines.append("")

    for owner, field in vector_fields:
        name = bridge._vector_name(owner, field)
        lines.append(f"PySequenceMethods& seq_{name}() {{")
        lines.append("    static PySequenceMethods methods = {};")
        lines.append("    return methods;")
        lines.append("}")
        lines.append(f"PyTypeObject& {bridge._vector_type_object_name(owner, field)}() {{")
        lines.append("    static PyTypeObject type = make_py_type_object();")
        lines.append("    return type;")
        lines.append("}")
        lines.append("")

    return "\n".join(lines).rstrip()


def _generate_field_getter(owner: BridgeType, field: BridgeField, types: dict[str, BridgeType]) -> str:
    cpp_type = bridge._cpp_type(owner)
    access = bridge._field_access(owner, field)
    anchor_expression = f"proxy_anchor<{cpp_type}>(self)"
    if field.optional:
        converted = bridge._return_value_code(field.type_ref, f"*({access})", types, anchor_expression)
        body = f"""if (!{access}) {{
        Py_RETURN_NONE;
    }}
    {converted}"""
    elif (owner.kind == "struct" and field.type_ref.kind == "struct"
            and field.type_ref.resolved is not None):
        nested = types[field.type_ref.resolved]
        body = (
            f"return {bridge._make_proxy_function_name(nested)}("
            f"std::addressof({access}), {anchor_expression});"
        )
    elif field.type_ref.kind == "vector":
        vector_type = bridge._vector_cpp_type(field.type_ref, types)
        body = (
            f"return make_native_vector_proxy<{vector_type}>("
            f"{bridge._vector_type_object_expression(owner, field)}, "
            f"std::addressof({access}), {anchor_expression});"
        )
    else:
        body = bridge._return_value_code(field.type_ref, access, types, anchor_expression)

    return f"""PyObject* {bridge._field_getter_name(owner, field)}(PyObject* self, void*) {{
    const auto* proxy = reinterpret_cast<const native_proxy_object<{cpp_type}>*>(self);
    const auto* value = proxy->value;
    if (value == nullptr) {{
        PyErr_SetString(PyExc_RuntimeError, "__SDK_NAME__ payload proxy is invalid");
        return nullptr;
    }}
    {body}
}}"""


def _generate_vector_proxy(owner: BridgeType, field: BridgeField, types: dict[str, BridgeType]) -> str:
    vector_type = bridge._vector_cpp_type(field.type_ref, types)
    name = bridge._vector_name(owner, field)
    len_fn = f"len_{name}"
    item_fn = f"item_{name}"
    ensure_fn = bridge._ensure_vector_function_name(owner, field)
    type_obj = bridge._vector_type_object_expression(owner, field)
    item_body = bridge._return_vector_item_code(
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
    cpp_type = bridge._cpp_type(bridge_type)
    type_obj = bridge._type_object_expression(bridge_type)
    ensure_fn = bridge._ensure_type_function_name(bridge_type)
    make_fn = bridge._make_proxy_function_name(bridge_type)
    getset_name = f"getsets_{bridge._mangle(bridge_type.qualified_name)}"

    getters = "\n\n".join(bridge._generate_field_getter(bridge_type, field, types) for field in bridge_type.fields)
    getset_entries = "\n".join(
        f"""    {{
        "{field.py_name}",
        reinterpret_cast<getter>({bridge._field_getter_name(bridge_type, field)}),
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
        type.tp_name = "__MODULE_NAME__.{bridge._mangle(bridge_type.qualified_name)}";
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


def _generate_proxy_definitions(types: dict[str, BridgeType], entries: list[SchemaEntry]) -> str:
    del entries
    proxy_types = [schema_type for schema_type in types.values() if schema_type.kind in {"table", "struct"}]
    vector_fields = [
        (schema_type, field)
        for schema_type in proxy_types
        for field in schema_type.fields
        if field.type_ref.kind == "vector"
    ]

    vector_defs = "\n\n".join(bridge._generate_vector_proxy(owner, field, types) for owner, field in vector_fields)
    type_defs = "\n\n".join(bridge._generate_type_proxy(schema_type, types) for schema_type in proxy_types)

    ensure_calls = "\n".join(
        f"        || !{bridge._ensure_vector_function_name(owner, field)}()"
        for owner, field in vector_fields
    )
    ensure_calls += "\n" + "\n".join(
        f"        || !{bridge._ensure_type_function_name(schema_type)}()"
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
        f"""    if (!add_python_type(python_module, {bridge._type_object_expression(schema_type)}, "{bridge._mangle(schema_type.qualified_name)}")) {{
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
