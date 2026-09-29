/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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
