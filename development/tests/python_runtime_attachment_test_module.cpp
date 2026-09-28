/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <Python.h>

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "PythonScriptOp.h"
#include "op/Op.h"
#include "opk/FrameResults.h"

#ifndef PYTHON_SCRIPT_OP_FIXTURES
#define PYTHON_SCRIPT_OP_FIXTURES ""
#endif

namespace {

opk::AttributeMap attributes(std::string_view script) {
    opk::AttributeMap result;
    result.set("script", (std::filesystem::path(PYTHON_SCRIPT_OP_FIXTURES) / script).string());
    return result;
}

bool runOperator(std::string &error) {
    opk::python::PythonScriptOp script;
    std::vector<opk::op::Op *> ops = {&script};

    if (const auto result = script.configure(attributes("empty_tensors.py")); !result) {
        error = result.error().info;
        return false;
    }
    if (const auto result = script.bind(0, ops); !result) {
        error = result.error().info;
        return false;
    }

    open_perception_kit::FrameResults results;
    opk::op::OpChainContext context;
    context.frameResults = &results;
    if (const auto result = script.process(context); !result) {
        error = result.error().info;
        return false;
    }
    return true;
}

PyObject *run(PyObject *, PyObject *) {
    try {
        std::string error;
        if (!runOperator(error) || !runOperator(error)) {
            PyErr_SetString(PyExc_RuntimeError, error.c_str());
            return nullptr;
        }
        Py_RETURN_NONE;
    } catch (const std::exception &error) {
        PyErr_SetString(PyExc_RuntimeError, error.what());
        return nullptr;
    }
}

std::array<PyMethodDef, 2> moduleMethods = {{
    {"run", run, METH_NOARGS, "Create and execute PythonScript Ops in the host interpreter."},
    {nullptr, nullptr, 0, nullptr},
}};

PyModuleDef moduleDefinition = {
    PyModuleDef_HEAD_INIT,
    "opk_python_runtime_attachment_test",
    "Python-hosted OPK runtime attachment regression fixture.",
    -1,
    moduleMethods.data(),
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

} // namespace

PyMODINIT_FUNC PyInit_opk_python_runtime_attachment_test() {
    return PyModule_Create(&moduleDefinition);
}
