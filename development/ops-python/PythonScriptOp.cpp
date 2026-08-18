/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "PythonScriptOp.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

#include <fmt/format.h>

#include "PythonRuntime.h"
#include "TensorBridge.h"
#include "perf/PerformanceMetrics.h"
#include "perf/PerformanceTracer.h"
#include "python_bridge/perception_python_bridge.h"

#ifndef PEK_DEVELOPMENT_PYTHON_PATH
#define PEK_DEVELOPMENT_PYTHON_PATH ""
#endif

namespace pek::python {
namespace {

std::atomic_uint64_t nextModuleId = 0;

std::string readScript(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Failed to open Python script: " + path.string());
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void appendUniquePath(std::vector<std::filesystem::path> &paths,
                      const std::filesystem::path &path) {
    if (path.empty() || !std::filesystem::is_directory(path))
        return;
    if (std::find(paths.begin(), paths.end(), path) == paths.end())
        paths.push_back(path);
}

} // namespace

PythonScriptOp::~PythonScriptOp() {
    releaseScript();
}

pek::Result<void> PythonScriptOp::configure(const pek::AttributeMap &attributes) {
    releaseScript();
    model = nullptr;

    try {
        scriptPath = attributes.getString("script");
        if (!std::filesystem::is_regular_file(scriptPath)) {
            return tl::unexpected(
                PEK_ERROR(pek::ErrorFlag::FileNotFound,
                          fmt::format("Python script does not exist: {}", scriptPath.string())));
        }

        pythonPaths.clear();
        appendUniquePath(pythonPaths, std::filesystem::absolute(scriptPath).parent_path());
        if (attributes.contains("pythonPaths")) {
            for (const auto &value : attributes.getArray("pythonPaths")) {
                const std::filesystem::path path(value.asString());
                if (!std::filesystem::is_directory(path)) {
                    return tl::unexpected(PEK_ERROR(
                        pek::ErrorFlag::FileNotFound,
                        fmt::format("Python import path does not exist: {}", path.string())));
                }
                appendUniquePath(pythonPaths, path);
            }
        }
        appendUniquePath(pythonPaths, packagedPythonPath());
        appendUniquePath(pythonPaths, PEK_DEVELOPMENT_PYTHON_PATH);
        moduleName = fmt::format("_pek_python_script_{}", nextModuleId.fetch_add(1));
        ensureRuntime();
    } catch (const pek::AttributeError &error) {
        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidOpChain,
                      fmt::format("Invalid PythonScript attributes: {}", error.what())));
    } catch (const std::exception &error) {
        return tl::unexpected(PEK_ERROR(pek::ErrorFlag::SystemFailure, error.what()));
    }

    return loadScript();
}

pek::Result<void> PythonScriptOp::loadScript() {
    try {
        const std::string source = readScript(scriptPath);
        const auto absolutePath = std::filesystem::absolute(scriptPath);
        GILGuard gil;
        PythonPathGuard pathGuard(pythonPaths);

        PyObjectPtr runtimeModule(PyImport_ImportModule("pek_python_ops"));
        if (!runtimeModule) {
            return tl::unexpected(
                PEK_ERROR(pek::ErrorFlag::SystemFailure,
                          "Failed to initialize Python tensor support:\n" + formatPythonError()));
        }

        PyObjectPtr newModule(PyModule_New(moduleName.c_str()));
        if (!newModule)
            return tl::unexpected(PEK_ERROR(pek::ErrorFlag::SystemFailure, formatPythonError()));

        PyObject *globals = PyModule_GetDict(newModule.get());
        PyObjectPtr fileName(PyUnicode_FromString(absolutePath.string().c_str()));
        PyObjectPtr packageName(PyUnicode_FromString(""));
        if (globals == nullptr || !fileName || !packageName ||
            PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins()) < 0 ||
            PyDict_SetItemString(globals, "__file__", fileName.get()) < 0 ||
            PyDict_SetItemString(globals, "__package__", packageName.get()) < 0 ||
            PyDict_SetItemString(PyImport_GetModuleDict(), moduleName.c_str(), newModule.get()) <
                0) {
            return tl::unexpected(PEK_ERROR(pek::ErrorFlag::SystemFailure, formatPythonError()));
        }

        PyObjectPtr code(
            Py_CompileString(source.c_str(), absolutePath.string().c_str(), Py_file_input));
        if (!code) {
            PyDict_DelItemString(PyImport_GetModuleDict(), moduleName.c_str());
            return tl::unexpected(PEK_ERROR(pek::ErrorFlag::ParseError,
                                            fmt::format("Failed to compile {}:\n{}",
                                                        scriptPath.string(),
                                                        formatPythonError())));
        }

        PyObjectPtr evaluation(PyEval_EvalCode(code.get(), globals, globals));
        if (!evaluation) {
            PyDict_DelItemString(PyImport_GetModuleDict(), moduleName.c_str());
            return tl::unexpected(PEK_ERROR(
                pek::ErrorFlag::ParseError,
                fmt::format("Failed to load {}:\n{}", scriptPath.string(), formatPythonError())));
        }

        PyObjectPtr callable(PyObject_GetAttrString(newModule.get(), "process"));
        if (!callable || !PyCallable_Check(callable.get())) {
            PyErr_Clear();
            PyDict_DelItemString(PyImport_GetModuleDict(), moduleName.c_str());
            return tl::unexpected(PEK_ERROR(
                pek::ErrorFlag::InvalidData,
                fmt::format("{} must define callable process(env, tensors)", scriptPath.string())));
        }

        module = newModule.release();
        processFunction = callable.release();
        return {};
    } catch (const std::exception &error) {
        return tl::unexpected(PEK_ERROR(pek::ErrorFlag::SystemFailure, error.what()));
    }
}

pek::Result<void> PythonScriptOp::bind(size_t index, const std::vector<pek::op::Op *> &ops) {
    model = nullptr;
    for (size_t upstream = index; upstream > 0; --upstream) {
        auto *inference = ops[upstream - 1]->as<pek::op::OpInterfaceInference>();
        if (inference != nullptr) {
            model = &inference->getModel();
            break;
        }
    }
    return {};
}

pek::Result<pek::op::OpSignal> PythonScriptOp::process(pek::op::OpChainContext &context) {
    const auto metricName = fmt::format("python/Script/{}", context.inferenceInfo.modelName);
    PEK_TRACE_SCOPE(metricName);
    PEK_PERF_SCOPE(metricName);

    if (module == nullptr || processFunction == nullptr) {
        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::SystemFailure, "Python script Op is not configured"));
    }
    if (context.frameResults == nullptr) {
        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidData, "Python script Op requires FrameResults"));
    }

    try {
        GILGuard gil;
        PythonPathGuard pathGuard(pythonPaths);
        perception::python_bridge::scoped_envelope envelope(*context.frameResults);
        PyObjectPtr tensors(wrapTensors(context, model));
        if (!tensors) {
            return tl::unexpected(
                PEK_ERROR(pek::ErrorFlag::TensorError,
                          "Failed to expose inference tensors:\n" + formatPythonError()));
        }

        PyObjectPtr result(PyObject_CallFunctionObjArgs(
            processFunction, envelope.py_object(), tensors.get(), nullptr));
        if (!result) {
            return tl::unexpected(PEK_ERROR(pek::ErrorFlag::GenericError,
                                            fmt::format("Python script failed: {}\n{}",
                                                        scriptPath.string(),
                                                        formatPythonError())));
        }
        if (result.get() != Py_None) {
            return tl::unexpected(PEK_ERROR(
                pek::ErrorFlag::InvalidData,
                fmt::format("{} process(env, tensors) must return None", scriptPath.string())));
        }
    } catch (const std::exception &error) {
        return tl::unexpected(PEK_ERROR(pek::ErrorFlag::SystemFailure, error.what()));
    }

    return pek::op::OpSignal::Continue;
}

void PythonScriptOp::releaseScript() noexcept {
    if (module == nullptr && processFunction == nullptr)
        return;
    if (!Py_IsInitialized()) {
        module = nullptr;
        processFunction = nullptr;
        return;
    }

    GILGuard gil;
    if (!moduleName.empty() &&
        PyDict_DelItemString(PyImport_GetModuleDict(), moduleName.c_str()) < 0) {
        PyErr_Clear();
    }
    Py_CLEAR(processFunction);
    Py_CLEAR(module);
}

} // namespace pek::python
