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

"""CPython module registration and host wrapper lifetime implementation."""

MODULE_TEMPLATE = """PyMethodDef* live_envelope_methods() {
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
