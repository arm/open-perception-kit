/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <numpy/arrayobject.h>

#include "TensorBridge.h"

#include <array>

#include "PythonBridgeError.h"
#include "PythonRuntime.h"

extern "C" PyObject *PyInit_opk_python_ops();

namespace opk::python {
namespace {

struct TensorObject {
    PyObject_HEAD PyObject *name;
    PyObject *array;
    Py_ssize_t index;
    double scale;
    double zeroPoint;
    int quantized;
};

struct ContextObject {
    PyObject_HEAD PyObject *producerInfo;
};

struct BridgeState {
    PyTypeObject *tensorType = nullptr;
    PyTypeObject *contextType = nullptr;
};

BridgeState &bridgeState() {
    static BridgeState state;
    return state;
}

void tensorDealloc(PyObject *self) {
    auto *tensor = reinterpret_cast<TensorObject *>(self);
    Py_XDECREF(tensor->name);
    Py_XDECREF(tensor->array);
    Py_TYPE(self)->tp_free(self);
}

PyObject *tensorRepr(PyObject *self) {
    auto *tensor = reinterpret_cast<TensorObject *>(self);
    return PyUnicode_FromFormat(
        "Tensor(index=%zd, name=%R, array=%R)", tensor->index, tensor->name, tensor->array);
}

PyObject *getName(PyObject *self, void *) {
    return Py_NewRef(reinterpret_cast<TensorObject *>(self)->name);
}

PyObject *getArray(PyObject *self, void *) {
    return Py_NewRef(reinterpret_cast<TensorObject *>(self)->array);
}

PyObject *getIndex(PyObject *self, void *) {
    return PyLong_FromSsize_t(reinterpret_cast<TensorObject *>(self)->index);
}

PyObject *getScale(PyObject *self, void *) {
    return PyFloat_FromDouble(reinterpret_cast<TensorObject *>(self)->scale);
}

PyObject *getZeroPoint(PyObject *self, void *) {
    return PyFloat_FromDouble(reinterpret_cast<TensorObject *>(self)->zeroPoint);
}

PyObject *getQuantized(PyObject *self, void *) {
    return PyBool_FromLong(reinterpret_cast<TensorObject *>(self)->quantized);
}

std::array<PyGetSetDef, 7> &tensorGetSet() {
    static std::array<PyGetSetDef, 7> definitions = {{
        {const_cast<char *>("name"),
         getName,
         nullptr,
         const_cast<char *>("Model output name."),
         nullptr},
        {const_cast<char *>("array"),
         getArray,
         nullptr,
         const_cast<char *>("Read-only NumPy view."),
         nullptr},
        {const_cast<char *>("index"),
         getIndex,
         nullptr,
         const_cast<char *>("Output index."),
         nullptr},
        {const_cast<char *>("scale"),
         getScale,
         nullptr,
         const_cast<char *>("Quantization scale."),
         nullptr},
        {const_cast<char *>("zero_point"),
         getZeroPoint,
         nullptr,
         const_cast<char *>("Quantization zero point."),
         nullptr},
        {const_cast<char *>("quantized"),
         getQuantized,
         nullptr,
         const_cast<char *>("Whether dequantization metadata applies."),
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    }};
    return definitions;
}

std::array<PyType_Slot, 4> &tensorSlots() {
    static std::array<PyType_Slot, 4> slots = {{
        {Py_tp_dealloc, reinterpret_cast<void *>(&tensorDealloc)},
        {Py_tp_repr, reinterpret_cast<void *>(&tensorRepr)},
        {Py_tp_getset, tensorGetSet().data()},
        {0, nullptr},
    }};
    return slots;
}

PyType_Spec &tensorSpec() {
    static PyType_Spec spec = {
        .name = "opk_python_ops.Tensor",
        .basicsize = sizeof(TensorObject),
        .itemsize = 0,
        .flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_IMMUTABLETYPE,
        .slots = tensorSlots().data(),
    };
    return spec;
}

void contextDealloc(PyObject *self) {
    auto *context = reinterpret_cast<ContextObject *>(self);
    Py_XDECREF(context->producerInfo);
    Py_TYPE(self)->tp_free(self);
}

PyObject *contextRepr(PyObject *self) {
    const auto *context = reinterpret_cast<ContextObject *>(self);
    return PyUnicode_FromFormat("Context(producer_info=%R)", context->producerInfo);
}

PyObject *getProducerInfo(PyObject *self, void *) {
    return Py_NewRef(reinterpret_cast<ContextObject *>(self)->producerInfo);
}

PyObject *pythonScript(PyObject *, PyObject *callback) {
    if (!PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "python_script expects a callable");
        return nullptr;
    }
    return Py_NewRef(callback);
}

std::array<PyMethodDef, 2> &moduleMethods() {
    static std::array<PyMethodDef, 2> // NOSONAR: function-local static cannot be inline.
        definitions = {{
            {"python_script",
             pythonScript,
             METH_O,
             "Mark a callable as a typed OPK Python script entry point."},
            {nullptr, nullptr, 0, nullptr},
        }};
    return definitions;
}

std::array<PyGetSetDef, 2> &contextGetSet() {
    static std::array<PyGetSetDef, 2> definitions = {{
        {const_cast<char *>("producer_info"),
         getProducerInfo,
         nullptr,
         const_cast<char *>("Producer identity for payloads created by this operation."),
         nullptr},
        {nullptr, nullptr, nullptr, nullptr, nullptr},
    }};
    return definitions;
}

std::array<PyType_Slot, 4> &contextSlots() {
    static std::array<PyType_Slot, 4> slots = {{
        {Py_tp_dealloc, reinterpret_cast<void *>(&contextDealloc)},
        {Py_tp_repr, reinterpret_cast<void *>(&contextRepr)},
        {Py_tp_getset, contextGetSet().data()},
        {0, nullptr},
    }};
    return slots;
}

PyType_Spec &contextSpec() {
    static PyType_Spec spec = {
        .name = "opk_python_ops.Context",
        .basicsize = sizeof(ContextObject),
        .itemsize = 0,
        .flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_IMMUTABLETYPE,
        .slots = contextSlots().data(),
    };
    return spec;
}

int numpyType(opk::Dtype type) {
    using enum opk::Dtype;

    switch (type) {
    case Uint8:
        return NPY_UINT8;
    case Int8:
        return NPY_INT8;
    case Float16:
        return NPY_FLOAT16;
    case Float32:
        return NPY_FLOAT32;
    case Int64:
        return NPY_INT64;
    }
    return NPY_NOTYPE;
}

PyObject *createTensor(size_t index, const opk::TensorView &view, const opk::Model *model) {
    PyTypeObject *tensorType = bridgeState().tensorType;
    if (tensorType == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "opk_python_ops.Tensor is not initialized");
        return nullptr;
    }

    const auto shape = view.getShape();
    std::array<npy_intp, 8> dimensions{};
    for (size_t dimension = 0; dimension < shape.rank; ++dimension) {
        if (shape.dims[dimension] <= 0) {
            PyErr_Format(PyExc_ValueError,
                         "Tensor %zu has invalid runtime shape %s",
                         index,
                         shape.toString().c_str());
            return nullptr;
        }
        dimensions[dimension] = shape.dims[dimension];
    }

    const int type = numpyType(view.getValueType());
    if (type == NPY_NOTYPE) {
        PyErr_Format(PyExc_TypeError, "Tensor %zu has an unsupported element type", index);
        return nullptr;
    }

    PyObject *array = PyArray_SimpleNewFromData(static_cast<int>(shape.rank),
                                                dimensions.data(),
                                                type,
                                                const_cast<uint8_t *>(view.getData()));
    if (array == nullptr)
        return nullptr;

    PyObject *readOnlyMemory =
        PyMemoryView_FromMemory(reinterpret_cast<char *>(const_cast<uint8_t *>(view.getData())),
                                static_cast<Py_ssize_t>(view.getByteCount()),
                                PyBUF_READ);
    if (readOnlyMemory == nullptr ||
        PyArray_SetBaseObject(reinterpret_cast<PyArrayObject *>(array), readOnlyMemory) < 0) {
        Py_XDECREF(readOnlyMemory);
        Py_DECREF(array);
        return nullptr;
    }
    PyArray_CLEARFLAGS(reinterpret_cast<PyArrayObject *>(array), NPY_ARRAY_WRITEABLE);

    auto *tensor = reinterpret_cast<TensorObject *>(tensorType->tp_alloc(tensorType, 0));
    if (tensor == nullptr) {
        Py_DECREF(array);
        return nullptr;
    }
    tensor->name = Py_NewRef(Py_None);
    if (model != nullptr && index < model->outputs.size() && !model->outputs[index].name.empty()) {
        Py_DECREF(tensor->name);
        tensor->name = PyUnicode_FromString(model->outputs[index].name.c_str());
        if (tensor->name == nullptr) {
            Py_DECREF(array);
            Py_DECREF(reinterpret_cast<PyObject *>(tensor));
            return nullptr;
        }
    }
    tensor->array = array;
    tensor->index = static_cast<Py_ssize_t>(index);
    tensor->scale = view.getScale();
    tensor->zeroPoint = view.getZeroPoint();
    tensor->quantized =
        view.getValueType() == opk::Dtype::Uint8 || view.getValueType() == opk::Dtype::Int8;
    return reinterpret_cast<PyObject *>(tensor);
}

PyModuleDef &moduleDefinition() {
    static PyModuleDef definition = {
        PyModuleDef_HEAD_INIT,
        "opk_python_ops",
        "Runtime objects passed to OPK Python script Ops.",
        -1,
        moduleMethods().data(),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
    };
    return definition;
}

PyObject *initializeModule() {
    if (_import_array() < 0)
        return nullptr;

    PyObject *pythonModule = PyModule_Create(&moduleDefinition());
    if (pythonModule == nullptr)
        return nullptr;

    PyObject *typingModule = PyImport_ImportModule("typing");
    PyObject *processCallback =
        typingModule ? PyObject_GetAttrString(typingModule, "Callable") : nullptr;
    if (processCallback == nullptr ||
        PyModule_AddObjectRef(pythonModule, "ProcessCallback", processCallback) < 0) {
        Py_XDECREF(processCallback);
        Py_XDECREF(typingModule);
        Py_DECREF(pythonModule);
        return nullptr;
    }
    Py_DECREF(processCallback);
    Py_DECREF(typingModule);

    PyObject *type = PyType_FromSpec(&tensorSpec());
    if (type == nullptr || PyModule_AddObjectRef(pythonModule, "Tensor", type) < 0) {
        Py_XDECREF(type);
        Py_DECREF(pythonModule);
        return nullptr;
    }
    PyObject *context = PyType_FromSpec(&contextSpec());
    if (context == nullptr || PyModule_AddObjectRef(pythonModule, "Context", context) < 0) {
        Py_XDECREF(context);
        Py_DECREF(type);
        Py_DECREF(pythonModule);
        return nullptr;
    }

    BridgeState &state = bridgeState();
    state.tensorType = reinterpret_cast<PyTypeObject *>(type);
    state.contextType = reinterpret_cast<PyTypeObject *>(context);
    Py_DECREF(type);
    Py_DECREF(context);
    return pythonModule;
}

} // namespace

void appendTensorModuleInittab() {
    if (PyImport_AppendInittab("opk_python_ops", &::PyInit_opk_python_ops) != 0)
        throw PythonBridgeError("Failed to register opk_python_ops Python module");
}

void initializeTensorModule() {
    if (!Py_IsInitialized())
        throw PythonBridgeError("Cannot initialize opk_python_ops without a Python interpreter");

    PyObject *modules = PyImport_GetModuleDict();
    if (PyDict_GetItemString(modules, "opk_python_ops") != nullptr)
        return;

    PyObject *module = ::PyInit_opk_python_ops();
    if (module == nullptr)
        throw PythonBridgeError("Failed to initialize opk_python_ops Python module: " +
                                formatPythonError());
    if (PyDict_SetItemString(modules, "opk_python_ops", module) < 0) {
        Py_DECREF(module);
        bridgeState() = {};
        throw PythonBridgeError("Failed to register opk_python_ops in sys.modules: " +
                                formatPythonError());
    }
    Py_DECREF(module);
}

PyObject *wrapTensors(const opk::op::OpChainContext &context, const opk::Model *model) {
    if (context.inferenceOutputTensorCount > context.inferenceOutputTensors.size()) {
        PyErr_SetString(PyExc_ValueError, "Inference tensor count exceeds the context capacity");
        return nullptr;
    }

    PyObject *result = PyTuple_New(static_cast<Py_ssize_t>(context.inferenceOutputTensorCount));
    if (result == nullptr)
        return nullptr;

    for (size_t index = 0; index < context.inferenceOutputTensorCount; ++index) {
        const auto &view = context.inferenceOutputTensors[index];
        if (!view.isValid()) {
            Py_DECREF(result);
            PyErr_Format(PyExc_ValueError, "Inference output tensor %zu is invalid", index);
            return nullptr;
        }
        PyObject *tensor = createTensor(index, view, model);
        if (tensor == nullptr) {
            Py_DECREF(result);
            return nullptr;
        }
        PyTuple_SET_ITEM(result, static_cast<Py_ssize_t>(index), tensor);
    }
    return result;
}

PyObject *wrapContext(PyObject *producerInfo) {
    if (producerInfo == nullptr) {
        PyErr_SetString(PyExc_ValueError, "producer_info is required");
        return nullptr;
    }
    PyTypeObject *contextType = bridgeState().contextType;
    if (contextType == nullptr) {
        PyErr_SetString(PyExc_RuntimeError, "opk_python_ops.Context is not initialized");
        return nullptr;
    }

    auto *context = reinterpret_cast<ContextObject *>(contextType->tp_alloc(contextType, 0));
    if (context == nullptr)
        return nullptr;
    context->producerInfo = Py_NewRef(producerInfo);
    return reinterpret_cast<PyObject *>(context);
}

} // namespace opk::python

extern "C" PyObject *PyInit_opk_python_ops() {
    return opk::python::initializeModule();
}
