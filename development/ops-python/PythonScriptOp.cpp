/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "PythonScriptOp.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <iterator>
#include <utility>

#include <fmt/format.h>

#include "PythonBridgeError.h"
#include "PythonRuntime.h"
#include "TensorBridge.h"
#include "op/OpChainDescriptor.h"
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
        throw PythonBridgeError("Failed to open Python script: " + path.string());
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void appendUniquePath(std::vector<std::filesystem::path> &paths,
                      const std::filesystem::path &path) {
    if (path.empty() || !std::filesystem::is_directory(path))
        return;
    if (std::ranges::find(paths, path) == paths.end())
        paths.push_back(path);
}

void removeModule(const std::string &moduleName) noexcept {
    if (!moduleName.empty() &&
        PyDict_DelItemString(PyImport_GetModuleDict(), moduleName.c_str()) < 0) {
        PyErr_Clear();
    }
}

PyObjectPtr makePythonProducerInfo(const std::string &instanceId,
                                   const std::string &component,
                                   const std::string &implementation) {
    PyObjectPtr producerModule(
        PyImport_ImportModule("perception.fb.perception.metadata.ProducerInfo"));
    PyObjectPtr producerType(
        producerModule ? PyObject_GetAttrString(producerModule.get(), "ProducerInfoT") : nullptr);
    if (!producerType || !PyCallable_Check(producerType.get()))
        return PyObjectPtr();
    return PyObjectPtr(PyObject_CallFunction(
        producerType.get(), "sss", instanceId.c_str(), component.c_str(), implementation.c_str()));
}

pek::Result<void> validateProcessSignature(PyObject *processFunction,
                                           const std::filesystem::path &scriptPath) {
    PyObjectPtr inspectModule(PyImport_ImportModule("inspect"));
    PyObjectPtr signatureFunction(
        inspectModule ? PyObject_GetAttrString(inspectModule.get(), "signature") : nullptr);
    if (!signatureFunction || !PyCallable_Check(signatureFunction.get())) {
        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::SystemFailure,
                      "Failed to initialize Python signature validation:\n" + formatPythonError()));
    }

    PyObjectPtr signature(
        PyObject_CallFunctionObjArgs(signatureFunction.get(), processFunction, nullptr));
    if (!signature) {
        return tl::unexpected(PEK_ERROR(pek::ErrorFlag::InvalidData,
                                        fmt::format("Cannot inspect {} process signature:\n{}",
                                                    scriptPath.string(),
                                                    formatPythonError())));
    }

    PyObjectPtr bindFunction(PyObject_GetAttrString(signature.get(), "bind"));
    if (PyObjectPtr boundArguments(bindFunction
                                       ? PyObject_CallFunctionObjArgs(
                                             bindFunction.get(), Py_None, Py_None, Py_None, nullptr)
                                       : nullptr);
        !boundArguments) {
        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("{} process must accept three positional arguments "
                                  "(env, tensors, context):\n{}",
                                  scriptPath.string(),
                                  formatPythonError())));
    }

    return {};
}

} // namespace

class LoadedScript {
  public:
    static pek::Result<std::unique_ptr<LoadedScript>>
    load(const std::filesystem::path &scriptPath,
         const std::vector<std::filesystem::path> &pythonPaths,
         std::string moduleName) {
        try {
            const std::string source = readScript(scriptPath);
            const auto absolutePath = std::filesystem::absolute(scriptPath);
            GILGuard gil;
            PythonPathTemplate pathTemplate(pythonPaths);
            PythonPathGuard pathGuard(pathTemplate);

            if (PyObjectPtr runtimeModule(PyImport_ImportModule("pek_python_ops"));
                !runtimeModule) {
                return tl::unexpected(PEK_ERROR(pek::ErrorFlag::SystemFailure,
                                                "Failed to initialize Python operation support:\n" +
                                                    formatPythonError()));
            }

            PyObjectPtr scriptModule(PyModule_New(moduleName.c_str()));
            if (!scriptModule)
                return tl::unexpected(
                    PEK_ERROR(pek::ErrorFlag::SystemFailure, formatPythonError()));

            PyObject *globals = PyModule_GetDict(scriptModule.get());
            PyObjectPtr fileName(PyUnicode_FromString(absolutePath.string().c_str()));
            if (PyObjectPtr packageName(PyUnicode_FromString(""));
                globals == nullptr || !fileName || !packageName ||
                PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins()) < 0 ||
                PyDict_SetItemString(globals, "__file__", fileName.get()) < 0 ||
                PyDict_SetItemString(globals, "__package__", packageName.get()) < 0 ||
                PyDict_SetItemString(
                    PyImport_GetModuleDict(), moduleName.c_str(), scriptModule.get()) < 0) {
                return tl::unexpected(
                    PEK_ERROR(pek::ErrorFlag::SystemFailure, formatPythonError()));
            }

            PyObjectPtr code(
                Py_CompileString(source.c_str(), absolutePath.string().c_str(), Py_file_input));
            if (!code) {
                removeModule(moduleName);
                return tl::unexpected(PEK_ERROR(pek::ErrorFlag::ParseError,
                                                fmt::format("Failed to compile {}:\n{}",
                                                            scriptPath.string(),
                                                            formatPythonError())));
            }

            if (PyObjectPtr evaluation(PyEval_EvalCode(code.get(), globals, globals));
                !evaluation) {
                removeModule(moduleName);
                return tl::unexpected(PEK_ERROR(pek::ErrorFlag::ParseError,
                                                fmt::format("Failed to load {}:\n{}",
                                                            scriptPath.string(),
                                                            formatPythonError())));
            }

            PyObjectPtr processFunction(PyObject_GetAttrString(scriptModule.get(), "process"));
            if (!processFunction || !PyCallable_Check(processFunction.get())) {
                PyErr_Clear();
                removeModule(moduleName);
                return tl::unexpected(
                    PEK_ERROR(pek::ErrorFlag::InvalidData,
                              fmt::format("{} must define callable process(env, tensors, context)",
                                          scriptPath.string())));
            }

            if (const auto signatureResult =
                    validateProcessSignature(processFunction.get(), scriptPath);
                !signatureResult) {
                removeModule(moduleName);
                return tl::unexpected(signatureResult.error());
            }

            return std::make_unique<LoadedScript>(std::move(moduleName),
                                                  std::move(scriptModule),
                                                  std::move(processFunction),
                                                  std::move(pathTemplate));
        } catch (const std::exception &error) {
            return tl::unexpected(PEK_ERROR(pek::ErrorFlag::SystemFailure, error.what()));
        }
    }

    LoadedScript(const LoadedScript &) = delete;
    LoadedScript &operator=(const LoadedScript &) = delete;

    LoadedScript(std::string moduleName,
                 PyObjectPtr moduleObject,
                 PyObjectPtr callable,
                 PythonPathTemplate pathTemplate)
        : moduleName(std::move(moduleName)), moduleObject(std::move(moduleObject)),
          callable(std::move(callable)), pathTemplate(std::move(pathTemplate)) {}

    ~LoadedScript() {
        reset();
    }

    [[nodiscard]] PyObject *processFunction() const noexcept {
        return callable.get();
    }

    [[nodiscard]] const PythonPathTemplate &pythonPathTemplate() const noexcept {
        return pathTemplate;
    }

  private:
    void reset() noexcept {
        if (!moduleObject && !callable)
            return;
        if (!Py_IsInitialized()) {
            static_cast<void>(moduleObject.release());
            static_cast<void>(callable.release());
            pathTemplate.release();
            return;
        }

        GILGuard gil;
        removeModule(moduleName);
        callable = PyObjectPtr();
        moduleObject = PyObjectPtr();
        pathTemplate = PythonPathTemplate();
    }

    std::string moduleName;
    PyObjectPtr moduleObject;
    PyObjectPtr callable;
    PythonPathTemplate pathTemplate;
};

PythonScriptOp::PythonScriptOp() = default;
PythonScriptOp::~PythonScriptOp() = default;

pek::Result<void> PythonScriptOp::configure(const pek::AttributeMap &attributes) {
    try {
        const std::filesystem::path candidateScriptPath = attributes.getString("script");
        if (!std::filesystem::is_regular_file(candidateScriptPath)) {
            return tl::unexpected(PEK_ERROR(
                pek::ErrorFlag::FileNotFound,
                fmt::format("Python script does not exist: {}", candidateScriptPath.string())));
        }

        std::vector<std::filesystem::path> candidatePythonPaths;
        appendUniquePath(candidatePythonPaths,
                         std::filesystem::absolute(candidateScriptPath).parent_path());
        if (attributes.contains("pythonPaths")) {
            for (const auto &value : attributes.getArray("pythonPaths")) {
                const std::filesystem::path path(value.asString());
                if (!std::filesystem::is_directory(path)) {
                    return tl::unexpected(PEK_ERROR(
                        pek::ErrorFlag::FileNotFound,
                        fmt::format("Python import path does not exist: {}", path.string())));
                }
                appendUniquePath(candidatePythonPaths, path);
            }
        }
        appendUniquePath(candidatePythonPaths, packagedPythonPath());
        appendUniquePath(candidatePythonPaths, PEK_DEVELOPMENT_PYTHON_PATH);
        if (instanceId.empty())
            instanceId = pek::op::makeDefaultInstanceId("pek-python-ops/PythonScript", 0);

        ensureRuntime();
        auto candidateScript =
            LoadedScript::load(candidateScriptPath,
                               candidatePythonPaths,
                               fmt::format("_pek_python_script_{}", nextModuleId.fetch_add(1)));
        if (!candidateScript)
            return tl::unexpected(candidateScript.error());

        scriptPath = candidateScriptPath;
        loadedScript = std::move(*candidateScript);
        model = nullptr;
    } catch (const pek::AttributeError &error) {
        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidOpChain,
                      fmt::format("Invalid PythonScript attributes: {}", error.what())));
    } catch (const std::exception &error) {
        return tl::unexpected(PEK_ERROR(pek::ErrorFlag::SystemFailure, error.what()));
    }

    return {};
}

pek::Result<void> PythonScriptOp::bind(size_t index, const std::vector<pek::op::Op *> &ops) {
    model = nullptr;
    for (size_t upstream = index; upstream > 0; --upstream) {
        const auto *inference = ops[upstream - 1]->as<pek::op::OpInterfaceInference>();
        if (inference != nullptr) {
            model = &inference->getModel();
            break;
        }
    }
    return {};
}

pek::Result<pek::op::OpSignal> PythonScriptOp::process(pek::op::OpChainContext &context) {
    const auto metricName =
        fmt::format("python/Script/{}/{}", context.inferenceInfo.modelName, instanceId);
    PEK_TRACE_SCOPE(metricName);
    PEK_PERF_SCOPE(metricName);

    if (!loadedScript) {
        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::SystemFailure, "Python script Op is not configured"));
    }
    if (context.frameResults == nullptr) {
        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidData, "Python script Op requires FrameResults"));
    }

    try {
        GILGuard gil;
        PythonPathGuard pathGuard(loadedScript->pythonPathTemplate());
        perception::python_bridge::scoped_envelope envelope(*context.frameResults);
        const auto producer = producerInfo(context.inferenceInfo.inferElementId,
                                           scriptPath.filename().string(),
                                           "pek-python-ops/PythonScript");
        PyObjectPtr pythonProducerInfo(makePythonProducerInfo(
            producer.instance_id, producer.component, producer.implementation));
        PyObjectPtr scriptContext(pythonProducerInfo ? wrapContext(pythonProducerInfo.get())
                                                     : nullptr);
        if (!scriptContext) {
            return tl::unexpected(
                PEK_ERROR(pek::ErrorFlag::SystemFailure,
                          "Failed to expose Python operation context:\n" + formatPythonError()));
        }
        PyObjectPtr tensors(wrapTensors(context, model));
        if (!tensors) {
            return tl::unexpected(
                PEK_ERROR(pek::ErrorFlag::TensorError,
                          "Failed to expose inference tensors:\n" + formatPythonError()));
        }

        PyObjectPtr result(PyObject_CallFunctionObjArgs(loadedScript->processFunction(),
                                                        envelope.py_object(),
                                                        tensors.get(),
                                                        scriptContext.get(),
                                                        nullptr));
        if (!result) {
            return tl::unexpected(PEK_ERROR(pek::ErrorFlag::GenericError,
                                            fmt::format("Python script failed: {}\n{}",
                                                        scriptPath.string(),
                                                        formatPythonError())));
        }
        if (result.get() != Py_None) {
            return tl::unexpected(
                PEK_ERROR(pek::ErrorFlag::InvalidData,
                          fmt::format("{} process(env, tensors, context) must return None",
                                      scriptPath.string())));
        }
    } catch (const std::exception &error) {
        return tl::unexpected(PEK_ERROR(pek::ErrorFlag::SystemFailure, error.what()));
    }

    return pek::op::OpSignal::Continue;
}

} // namespace pek::python
