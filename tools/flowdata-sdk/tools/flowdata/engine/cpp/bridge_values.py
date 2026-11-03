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

"""C++ type spelling and value-conversion fragments for bridge proxies."""
from __future__ import annotations

from . import python_bridge as bridge
from .python_bridge import BridgeField, BridgeType, BridgeTypeRef


def _mangle(value: str) -> str:
    # Injective encoding with no underscores, so composing symbols stays unambiguous.
    return "n" + value.encode("utf-8").hex()


def _cpp_type(bridge_type: BridgeType) -> str:
    return bridge_type.cpp_type


def _python_fb_module(sdk_name: str, bridge_type: BridgeType) -> str:
    if bridge_type.namespace:
        return f"{sdk_name}.fb.{bridge_type.namespace}.{bridge_type.name}"
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
    if type_ref.kind in bridge._SCALAR_TYPES.values():
        return bridge._cpp_scalar_type(type_ref.kind)
    if type_ref.kind == "string":
        return "std::string"
    if type_ref.kind in {"table", "struct", "enum"} and type_ref.resolved is not None:
        return bridge._cpp_type(types[type_ref.resolved])
    bridge.fail(f"internal generator error: unsupported bridge type reference {type_ref}")


def _vector_cpp_type(type_ref: BridgeTypeRef, types: dict[str, BridgeType]) -> str:
    if type_ref.kind != "vector" or type_ref.element is None:
        bridge.fail("internal generator error: vector C++ type requested for non-vector field")

    element = type_ref.element
    element_cpp = bridge._ref_cpp_type(element, types)
    if element.kind == "table":
        return f"std::vector<std::unique_ptr<{element_cpp}>>"
    return f"std::vector<{element_cpp}>"


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
    if bridge._is_signed_integer(type_ref.kind):
        return f"return py_long_from_signed({expr});"
    if bridge._is_unsigned_integer(type_ref.kind):
        return f"return py_long_from_unsigned({expr});"
    if type_ref.kind in {"float", "double"}:
        return f"return PyFloat_FromDouble(static_cast<double>({expr}));"
    if type_ref.kind == "enum":
        return f"return py_long_from_enum({expr});"
    bridge.fail(f"internal generator error: unsupported scalar bridge type {type_ref.kind}")


def _return_value_code(
    type_ref: BridgeTypeRef,
    expr: str,
    types: dict[str, BridgeType],
    anchor_expression: str,
) -> str:
    if type_ref.kind == "string":
        return f"return py_string_from_std({expr});"
    if type_ref.kind in bridge._SCALAR_TYPES.values() or type_ref.kind == "enum":
        return bridge._return_scalar_code(type_ref, expr)
    if type_ref.kind in {"table", "struct"} and type_ref.resolved is not None:
        nested = types[type_ref.resolved]
        make_fn = bridge._make_proxy_function_name(nested)
        return f"""const auto* nested = {expr}.get();
    if (nested == nullptr) {{
        Py_RETURN_NONE;
    }}
    return {make_fn}(nested, {anchor_expression});"""
    bridge.fail(f"cannot generate C++ Python bridge getter for unsupported field type {type_ref.kind}")


def _return_vector_item_code(
    type_ref: BridgeTypeRef,
    types: dict[str, BridgeType],
    anchor_expression: str,
) -> str:
    if type_ref.element is None:
        bridge.fail("internal generator error: vector item requested without element type")
    element = type_ref.element

    if element.kind == "table" and element.resolved is not None:
        nested = types[element.resolved]
        return f"""const auto& item = (*vector)[static_cast<std::size_t>(index)];
    if (!item) {{
        Py_RETURN_NONE;
    }}
    return {bridge._make_proxy_function_name(nested)}(item.get(), {anchor_expression});"""

    if element.kind == "struct" and element.resolved is not None:
        nested = types[element.resolved]
        return f"""const auto& item = (*vector)[static_cast<std::size_t>(index)];
    return {bridge._make_proxy_function_name(nested)}(std::addressof(item), {anchor_expression});"""

    return bridge._return_value_code(
        element,
        "(*vector)[static_cast<std::size_t>(index)]",
        types,
        anchor_expression,
    )
