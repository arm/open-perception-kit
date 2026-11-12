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

"""Bridge-specific schema parsing and resolution; no C++ emission or output IO."""
from __future__ import annotations

from pathlib import Path
import keyword

from ..types import GenerationContext
from ..discovery import _mask_strings
from . import python_bridge as bridge
from .python_bridge import BridgeField, BridgeType, BridgeTypeRef
from .identifiers import cpp_identifier


def _strip_comments(text: str) -> str:
    result: list[str] = []
    i = 0
    in_string = False
    while i < len(text):
        char = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""

        if in_string:
            result.append(char)
            if char == "\\" and i + 1 < len(text):
                result.append(text[i + 1])
                i += 2
                continue
            if char == '"':
                in_string = False
            i += 1
            continue

        if char == '"':
            in_string = True
            result.append(char)
            i += 1
            continue

        if char == "/" and nxt == "/":
            while i < len(text) and text[i] not in "\r\n":
                i += 1
            continue

        if char == "/" and nxt == "*":
            i += 2
            while i + 1 < len(text) and not (text[i] == "*" and text[i + 1] == "/"):
                i += 1
            i += 2
            continue

        result.append(char)
        i += 1

    return "".join(result)


def _find_matching_brace(text: str, open_index: int) -> int:
    depth = 0
    in_string = False
    i = open_index
    while i < len(text):
        char = text[i]
        if in_string:
            if char == "\\":
                i += 2
                continue
            if char == '"':
                in_string = False
            i += 1
            continue

        if char == '"':
            in_string = True
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1

    return -1


def _split_top_level_semicolons(text: str) -> list[str]:
    parts: list[str] = []
    start = 0
    paren_depth = 0
    bracket_depth = 0

    for i, char in enumerate(_mask_strings(text)):
        if char == "(":
            paren_depth += 1
        elif char == ")":
            paren_depth = max(0, paren_depth - 1)
        elif char == "[":
            bracket_depth += 1
        elif char == "]":
            bracket_depth = max(0, bracket_depth - 1)
        elif char == ";" and paren_depth == 0 and bracket_depth == 0:
            parts.append(text[start:i].strip())
            start = i + 1

    tail = text[start:].strip()
    if tail:
        parts.append(tail)
    return parts


def _schema_to_python_field_name(name: str) -> str:
    parts = name.split("_")
    value = parts[0] + "".join(part[:1].upper() + part[1:] for part in parts[1:])
    return f"{value}_" if keyword.iskeyword(value) else value


def _parse_type_ref(type_name: str) -> BridgeTypeRef:
    type_name = type_name.strip()
    if type_name.startswith("[") and type_name.endswith("]"):
        return bridge.BridgeTypeRef(
            kind="vector",
            name=type_name,
            element=bridge._parse_type_ref(type_name[1:-1]),
        )
    if type_name == "string":
        return bridge.BridgeTypeRef(kind="string", name=type_name)
    if type_name in bridge._SCALAR_TYPES:
        return bridge.BridgeTypeRef(kind=bridge._SCALAR_TYPES[type_name], name=type_name)
    return bridge.BridgeTypeRef(kind="named", name=type_name)


def _parse_fields(body: str, schema_path: Path, namespace: str, type_name: str) -> list[BridgeField]:
    fields: list[BridgeField] = []
    for statement in bridge._split_top_level_semicolons(body):
        if not statement or ":" not in statement:
            continue

        raw_name, raw_rest = statement.split(":", 1)
        if bridge.re.search(r"(?:\(|,)\s*deprecated\s*(?=[,)])", _mask_strings(raw_rest)):
            continue
        field_name = raw_name.strip()
        if not field_name or not bridge._CPP_IDENTIFIER_RE.fullmatch(field_name):
            bridge.fail(
                f"cannot generate C++ Python bridge for field '{field_name}' in "
                f"{schema_path}:{namespace}.{type_name}\n"
                "Fix: use field names that map directly to generated C++ object-api members."
            )

        raw_type = raw_rest.strip()
        for separator in ("=", "("):
            if separator in raw_type:
                raw_type = raw_type.split(separator, 1)[0].strip()

        if not raw_type:
            bridge.fail(
                f"cannot determine FlatBuffers field type for {namespace}.{type_name}.{field_name} "
                f"in {schema_path}"
            )

        fields.append(
            bridge.BridgeField(
                schema_name=field_name,
                py_name=bridge._schema_to_python_field_name(field_name),
                cpp_name=cpp_identifier(field_name),
                type_ref=bridge._parse_type_ref(raw_type),
                optional=bridge.re.search(r"=\s*null\b", _mask_strings(raw_rest)) is not None,
            )
        )
    return fields


def _parse_schema_file(schema_path: Path) -> list[BridgeType]:
    text = bridge._strip_comments(schema_path.read_text(encoding="utf-8"))
    declarations = _mask_strings(text)
    current_namespace = ""
    types: list[BridgeType] = []
    token_re = bridge.re.compile(r"\b(namespace|table|struct|enum)\b")
    pos = 0

    while True:
        match = token_re.search(declarations, pos)
        if match is None:
            break

        token = match.group(1)
        pos = match.end()

        if token == "namespace":
            semi = text.find(";", pos)
            if semi < 0:
                bridge.fail(f"unterminated namespace declaration in {schema_path}")
            current_namespace = text[pos:semi].strip()
            pos = semi + 1
            continue

        name_match = bridge.re.match(r"\s*([A-Za-z_][A-Za-z0-9_]*)", text[pos:])
        if name_match is None:
            bridge.fail(f"missing {token} name in {schema_path}")
        name = name_match.group(1)
        pos += name_match.end()

        open_brace = declarations.find("{", pos)
        if open_brace < 0:
            bridge.fail(f"missing '{{' for {token} {current_namespace}.{name} in {schema_path}")
        close_brace = bridge._find_matching_brace(text, open_brace)
        if close_brace < 0:
            bridge.fail(f"unterminated {token} {current_namespace}.{name} in {schema_path}")

        body = text[open_brace + 1: close_brace]
        qualified = f"{current_namespace}.{name}" if current_namespace else name

        if token in {"table", "struct"}:
            fields = bridge._parse_fields(body, schema_path, current_namespace, name)
            types.append(
                bridge.BridgeType(
                    kind=token,
                    namespace=current_namespace,
                    name=name,
                    qualified_name=qualified,
                    fields=fields,
                )
            )
        elif token == "enum":
            header = text[pos:open_brace]
            underlying = "int"
            if ":" in header:
                underlying = header.split(":", 1)[1].strip().split()[0]
            if underlying not in bridge._SCALAR_TYPES:
                bridge.fail(
                    f"cannot generate C++ Python bridge for enum {qualified}: "
                    f"unsupported underlying type '{underlying}'"
                )
            types.append(
                bridge.BridgeType(
                    kind="enum",
                    namespace=current_namespace,
                    name=name,
                    qualified_name=qualified,
                    fields=[],
                    enum_underlying=underlying,
                )
            )

        pos = close_brace + 1

    return types


def _resolve_named_type(name: str, namespace: str, types: dict[str, BridgeType]) -> str:
    # flatc searches enum namespaces first, then table/struct namespaces.
    parts = namespace.split(".") if namespace else []
    for enum_lookup in (True, False):
        for length in range(len(parts), -1, -1):
            candidate = ".".join([*parts[:length], name])
            if candidate in types and (types[candidate].kind == "enum") == enum_lookup:
                return candidate

    bridge.fail(
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
            bridge.fail("internal generator error: vector field without element type")
        return bridge.BridgeTypeRef(
            kind="vector",
            name=type_ref.name,
            element=bridge._resolve_type_ref(type_ref.element, namespace, types),
        )

    if type_ref.kind != "named":
        return type_ref

    resolved = bridge._resolve_named_type(type_ref.name, namespace, types)
    resolved_type = types[resolved]
    return bridge.BridgeTypeRef(kind=resolved_type.kind, name=type_ref.name, resolved=resolved)


def _schema_types(context: GenerationContext) -> dict[str, BridgeType]:
    parsed: dict[str, BridgeType] = {}
    for schema_path in context.schema_paths:
        for schema_type in bridge._parse_schema_file(schema_path):
            if schema_type.qualified_name in parsed:
                bridge.fail(
                    f"duplicate FlatBuffers type {schema_type.qualified_name} "
                    f"while generating the C++ Python bridge"
                )
            parsed[schema_type.qualified_name] = schema_type

    resolved: dict[str, BridgeType] = {}
    for qualified_name, schema_type in parsed.items():
        resolved_fields = [
            bridge.BridgeField(
                schema_name=field.schema_name,
                py_name=field.py_name,
                cpp_name=field.cpp_name,
                type_ref=bridge._resolve_type_ref(field.type_ref, schema_type.namespace, parsed),
                optional=field.optional,
            )
            for field in schema_type.fields
        ]
        resolved[qualified_name] = bridge.BridgeType(
            kind=schema_type.kind,
            namespace=schema_type.namespace,
            name=schema_type.name,
            qualified_name=qualified_name,
            fields=resolved_fields,
            enum_underlying=schema_type.enum_underlying,
        )

    return resolved
