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

"""Known-payload dispatch emission and its CPython runtime implementation.

The f-string is kept intact to preserve interpolation and evaluation order.
"""
from __future__ import annotations

from ..types import SchemaEntry
from . import python_bridge as bridge
from .python_bridge import BridgeType


def _generate_known_payload_helpers(
    entries: list[SchemaEntry],
    root_types: dict[str, BridgeType],
) -> str:
    enum_values = "\n".join(f"    {bridge._known_kind_name(entry)}," for entry in entries)
    type_matches = "\n".join(
        f"""    if (py_type_matches(type_object, "{bridge._python_fb_module('__SDK_NAME__', root_types[entry.qualified_root_type])}", "{root_types[entry.qualified_root_type].py_type_name}")) {{
        return known_payload_kind::{bridge._known_kind_name(entry)};
    }}"""
        for entry in entries
    )
    value_matches = "\n".join(
        f"""    if (py_object_type_matches(value, "{bridge._python_fb_module('__SDK_NAME__', root_types[entry.qualified_root_type])}", "{root_types[entry.qualified_root_type].py_type_name}")) {{
        return known_payload_kind::{bridge._known_kind_name(entry)};
    }}"""
        for entry in entries
    )

    count_cases = "\n".join(
        f"""        case known_payload_kind::{bridge._known_kind_name(entry)}:
            return PyLong_FromSize_t(live->envelope->count<{entry.native_type}>());"""
        for entry in entries
    )
    contains_cases = "\n".join(
        f"""        case known_payload_kind::{bridge._known_kind_name(entry)}:
            if (live->envelope->contains<{entry.native_type}>()) {{
                Py_RETURN_TRUE;
            }}
            Py_RETURN_FALSE;"""
        for entry in entries
    )
    get_cases = "\n".join(
        f"""        case known_payload_kind::{bridge._known_kind_name(entry)}:
            return get_known_payload<{entry.native_type}>(*live->envelope, {bridge._type_object_expression(root_types[entry.qualified_root_type])}, index);"""
        for entry in entries
    )
    foreach_cases = "\n".join(
        f"""        case known_payload_kind::{bridge._known_kind_name(entry)}:
            return list_known_payloads<{entry.native_type}>(*live->envelope, {bridge._type_object_expression(root_types[entry.qualified_root_type])});"""
        for entry in entries
    )
    add_cases = "\n".join(
        f"""        case known_payload_kind::{bridge._known_kind_name(entry)}:
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

        try {{
            live->envelope->add(
                *key,
                std::span<const std::uint8_t>(
                    static_cast<const std::uint8_t*>(view.buf),
                    static_cast<std::size_t>(view.len)
                )
            );
        }} catch (const std::exception& exc) {{
            PyBuffer_Release(&view);
            PyErr_SetString(PyExc_RuntimeError, exc.what());
            return nullptr;
        }}
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
