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

"""Bridge header and runtime support preceding schema-specific proxy definitions."""
from .bridge_module_template import MODULE_TEMPLATE

HEADER_TEMPLATE = """// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.

#pragma once

#include <Python.h>

#include "__SDK_HEADER__"

namespace __SDK_NAME__::python_bridge {

void append_inittab();

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
    case container::producer_identity_status::name_mismatch:
        value = "name_mismatch";
        break;
    case container::producer_identity_status::version_mismatch:
        value = "version_mismatch";
        break;
    case container::producer_identity_status::compatible_match:
        value = "compatible_match";
        break;
    }

    PyObject* sdk_module = PyImport_ImportModule("__SDK_NAME__.sdk");
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

    PyObject* sdk_module = PyImport_ImportModule("__SDK_NAME__");
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

""" + MODULE_TEMPLATE
