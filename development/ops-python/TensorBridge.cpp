/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <numpy/arrayobject.h>

#include "TensorBridge.h"

#include <array>
#include <stdexcept>

namespace pek::python {
namespace {

struct TensorObject {
    PyObject_HEAD PyObject *name;
    PyObject *array;
    Py_ssize_t index;
    double scale;
    double zeroPoint;
    int quantized;
};

PyTypeObject *tensorType = nullptr;

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

PyGetSetDef tensorGetSet[] = {
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
    {const_cast<char *>("index"), getIndex, nullptr, const_cast<char *>("Output index."), nullptr},
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
};

PyType_Slot tensorSlots[] = {
    {Py_tp_dealloc, reinterpret_cast<void *>(tensorDealloc)},
    {Py_tp_repr, reinterpret_cast<void *>(tensorRepr)},
    {Py_tp_getset, tensorGetSet},
    {0, nullptr},
};

PyType_Spec tensorSpec = {
    .name = "pek_python_ops.Tensor",
    .basicsize = sizeof(TensorObject),
    .itemsize = 0,
    .flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_IMMUTABLETYPE,
    .slots = tensorSlots,
};

int numpyType(pek::Dtype type) {
    switch (type) {
    case pek::Dtype::Uint8:
        return NPY_UINT8;
    case pek::Dtype::Int8:
        return NPY_INT8;
    case pek::Dtype::Float16:
        return NPY_FLOAT16;
    case pek::Dtype::Float32:
        return NPY_FLOAT32;
    case pek::Dtype::Int64:
        return NPY_INT64;
    }
    return NPY_NOTYPE;
}

PyObject *createTensor(size_t index, const pek::TensorView &view, const pek::Model *model) {
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
        view.getValueType() == pek::Dtype::Uint8 || view.getValueType() == pek::Dtype::Int8;
    return reinterpret_cast<PyObject *>(tensor);
}

PyModuleDef moduleDefinition = {
    PyModuleDef_HEAD_INIT,
    "pek_python_ops",
    "Runtime objects passed to PEK Python script Ops.",
    -1,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

PyObject *initializeModule() {
    if (_import_array() < 0)
        return nullptr;

    PyObject *module = PyModule_Create(&moduleDefinition);
    if (module == nullptr)
        return nullptr;

    PyObject *type = PyType_FromSpec(&tensorSpec);
    if (type == nullptr || PyModule_AddObjectRef(module, "Tensor", type) < 0) {
        Py_XDECREF(type);
        Py_DECREF(module);
        return nullptr;
    }
    tensorType = reinterpret_cast<PyTypeObject *>(type);
    Py_DECREF(type);
    return module;
}

} // namespace

extern "C" PyObject *PyInit_pek_python_ops() {
    return initializeModule();
}

void appendTensorModuleInittab() {
    if (PyImport_AppendInittab("pek_python_ops", &PyInit_pek_python_ops) != 0)
        throw std::runtime_error("Failed to register pek_python_ops Python module");
}

PyObject *wrapTensors(const pek::op::OpChainContext &context, const pek::Model *model) {
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

} // namespace pek::python
