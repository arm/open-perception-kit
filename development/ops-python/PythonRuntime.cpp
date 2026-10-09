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

#include "PythonRuntime.h"

#include <dlfcn.h>

#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include "PythonBridgeError.h"
#include "TensorBridge.h"
#include "python_bridge/open_perception_kit_python_bridge.h"

#ifndef OPK_PYTHON_EXECUTABLE
#define OPK_PYTHON_EXECUTABLE ""
#endif

#ifndef OPK_DEVELOPMENT_PYTHON_PATH
#define OPK_DEVELOPMENT_PYTHON_PATH ""
#endif

#ifndef OPK_INSTALLED_PYTHON_PATH
#define OPK_INSTALLED_PYTHON_PATH ""
#endif

#ifndef OPK_EXPECTED_NUMPY_VERSION
#define OPK_EXPECTED_NUMPY_VERSION ""
#endif

#ifndef OPK_EXPECTED_FLATBUFFERS_VERSION
#define OPK_EXPECTED_FLATBUFFERS_VERSION ""
#endif

namespace opk::python {
namespace {

std::once_flag initializationFlag;
void *pythonLibraryHandle = nullptr;

void exposePythonSymbols() {
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void *>(&Py_InitializeFromConfig), &info) == 0 ||
        info.dli_fname == nullptr) {
        throw PythonBridgeError("Failed to locate the embedded Python library");
    }

    pythonLibraryHandle = dlopen(
        info.dli_fname, RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE); // NOLINT(concurrency-mt-unsafe)
    if (pythonLibraryHandle == nullptr) {
        const char *error = dlerror();
        throw PythonBridgeError(std::string("Failed to expose embedded Python symbols: ") +
                                (error == nullptr ? "unknown error" : error));
    }
}

std::filesystem::path pythonExecutable() {
    if (const char *runtimeVenv = std::getenv("OPK_PYTHON_RUNTIME_VENV");
        runtimeVenv != nullptr && std::string_view(runtimeVenv).empty() == false)
        return std::filesystem::path(runtimeVenv) / "bin/python";
    return OPK_PYTHON_EXECUTABLE;
}

void initializeOwnedRuntime() {
    open_perception_kit::python_bridge::append_inittab();
    appendTensorModuleInittab();

    PyConfig config;
    PyConfig_InitPythonConfig(&config);
    PyStatus status = PyStatus_Ok();
    if (const auto executable = pythonExecutable(); !executable.empty()) {
        const auto executableString = executable.string();
        status = PyConfig_SetBytesString(&config, &config.program_name, executableString.c_str());
    }
    if (!PyStatus_Exception(status))
        status = Py_InitializeFromConfig(&config);

    const std::string error = status.err_msg == nullptr ? "unknown error" : status.err_msg;
    PyConfig_Clear(&config);
    if (PyStatus_Exception(status) || !Py_IsInitialized())
        throw PythonBridgeError("Failed to initialize embedded Python: " + error);

    PyEval_SaveThread();
}

void attachToRuntime(const std::vector<std::filesystem::path> &pythonPaths) {
    GILGuard gil;
    PythonPathTemplate pathTemplate(pythonPaths);
    PythonPathGuard pathGuard(pathTemplate);

    if (!open_perception_kit::python_bridge::initialize_module()) {
        throw PythonBridgeError("Failed to initialize open_perception_kit_bridge Python module: " +
                                formatPythonError());
    }
    initializeTensorModule();
}

void validateInstalledModule(const char *moduleName,
                             const char *expectedVersion,
                             const std::filesystem::path &runtimeRoot) {
    PyObjectPtr module(PyImport_ImportModule(moduleName));
    if (!module)
        throw PythonBridgeError(std::string("Failed to import required Python module ") +
                                moduleName + ": " + formatPythonError());

    PyObjectPtr version(PyObject_GetAttrString(module.get(), "__version__"));
    const char *actualVersion = version ? PyUnicode_AsUTF8(version.get()) : nullptr;
    if (actualVersion == nullptr || actualVersion != std::string_view(expectedVersion))
        throw PythonBridgeError(std::string("Python module ") + moduleName + " has version " +
                                (actualVersion == nullptr ? "unknown" : actualVersion) +
                                "; expected " + expectedVersion);

    PyObjectPtr file(PyObject_GetAttrString(module.get(), "__file__"));
    const char *moduleFile = file ? PyUnicode_AsUTF8(file.get()) : nullptr;
    if (moduleFile == nullptr)
        throw PythonBridgeError(std::string("Python module has no file path: ") + moduleName);

    const auto canonicalRoot = std::filesystem::weakly_canonical(runtimeRoot);
    const auto canonicalFile = std::filesystem::weakly_canonical(moduleFile);
    const auto relative = canonicalFile.lexically_relative(canonicalRoot);
    if (relative.empty() || *relative.begin() == "..")
        throw PythonBridgeError(std::string("Python module was loaded outside the OPK runtime: ") +
                                moduleName + " (" + canonicalFile.string() + ")");
}

void validateInstalledRuntime() {
    const auto runtimeRoot = installedPythonRuntimePath();
    if (runtimeRoot.empty())
        return;
    if (!std::filesystem::is_directory(runtimeRoot))
        throw PythonBridgeError("OPK Python runtime dependencies are not installed: " +
                                runtimeRoot.string());

    GILGuard gil;
    PythonPathTemplate pathTemplate({runtimeRoot});
    PythonPathGuard pathGuard(pathTemplate);
    validateInstalledModule("numpy", OPK_EXPECTED_NUMPY_VERSION, runtimeRoot);
    validateInstalledModule("flatbuffers", OPK_EXPECTED_FLATBUFFERS_VERSION, runtimeRoot);
}

void initializeRuntime(const std::vector<std::filesystem::path> &pythonPaths) {
    if (Py_IsInitialized()) {
        validateInstalledRuntime();
        attachToRuntime(pythonPaths);
        return;
    }
    exposePythonSymbols();
    initializeOwnedRuntime();
    validateInstalledRuntime();
}

class ScopedGILRelease {
  public:
    ScopedGILRelease() {
        if (Py_IsInitialized() && PyThreadState_GetUnchecked() != nullptr)
            threadState = PyEval_SaveThread();
    }

    ScopedGILRelease(const ScopedGILRelease &) = delete;
    ScopedGILRelease &operator=(const ScopedGILRelease &) = delete;

    ~ScopedGILRelease() {
        if (threadState != nullptr)
            PyEval_RestoreThread(threadState);
    }

  private:
    PyThreadState *threadState = nullptr;
};

} // namespace

void ensureRuntime(const std::vector<std::filesystem::path> &pythonPaths) {
    auto runtimePythonPaths = pythonPaths;
    ScopedGILRelease gilRelease;
    std::call_once(initializationFlag, [runtimePythonPaths = std::move(runtimePythonPaths)] {
        initializeRuntime(runtimePythonPaths);
    });
}

GILGuard::~GILGuard() {
    PyGILState_Release(state);
}

PyObjectPtr::PyObjectPtr(PyObject *object) noexcept : object(object) {}

PyObjectPtr::PyObjectPtr(PyObjectPtr &&other) noexcept
    : object(std::exchange(other.object, nullptr)) {}

PyObjectPtr &PyObjectPtr::operator=(PyObjectPtr &&other) noexcept {
    if (this != &other) {
        Py_XDECREF(object);
        object = std::exchange(other.object, nullptr);
    }
    return *this;
}

PyObjectPtr::~PyObjectPtr() {
    Py_XDECREF(object);
}

PyObject *PyObjectPtr::get() const noexcept {
    return object;
}

PyObject *PyObjectPtr::release() noexcept {
    return std::exchange(object, nullptr);
}

PyObjectPtr::operator bool() const noexcept {
    return object != nullptr;
}

PythonPathTemplate::PythonPathTemplate(const std::vector<std::filesystem::path> &paths) {
    sysModule = PyObjectPtr(PyImport_ImportModule("sys"));
    PyObjectPtr currentPath(sysModule ? PyObject_GetAttrString(sysModule.get(), "path") : nullptr);
    path = PyObjectPtr(currentPath ? PySequence_List(currentPath.get()) : nullptr);
    if (!currentPath || !path)
        throw PythonBridgeError("Failed to access Python sys.path: " + formatPythonError());

    for (auto iterator = paths.rbegin(); iterator != paths.rend(); ++iterator) {
        PyObjectPtr value(PyUnicode_FromString(iterator->string().c_str()));
        if (!value || PyList_Insert(path.get(), 0, value.get()) < 0)
            throw PythonBridgeError("Failed to prepare Python sys.path: " + formatPythonError());
    }
}

void PythonPathTemplate::release() noexcept {
    static_cast<void>(sysModule.release());
    static_cast<void>(path.release());
}

PythonPathGuard::PythonPathGuard(const PythonPathTemplate &pathTemplate)
    : sysModule(pathTemplate.sysModule.get()) {
    originalPathObject =
        PyObjectPtr(sysModule ? PyObject_GetAttrString(sysModule, "path") : nullptr);
    temporaryPath =
        PyObjectPtr(pathTemplate.path ? PySequence_List(pathTemplate.path.get()) : nullptr);
    if (!originalPathObject || !temporaryPath)
        throw PythonBridgeError("Failed to prepare temporary Python sys.path: " +
                                formatPythonError());
    if (PyObject_SetAttrString(sysModule, "path", temporaryPath.get()) < 0)
        throw PythonBridgeError("Failed to activate temporary Python sys.path: " +
                                formatPythonError());
}

PythonPathGuard::~PythonPathGuard() {
    if (sysModule == nullptr || !originalPathObject)
        return;
    if (PyObject_SetAttrString(sysModule, "path", originalPathObject.get()) < 0)
        PyErr_Clear();
}

std::string formatPythonError() {
    if (!PyErr_Occurred())
        return "Python operation failed without an exception";

    PyObject *rawType = nullptr;
    PyObject *rawValue = nullptr;
    PyObject *rawTraceback = nullptr;
    PyErr_Fetch(&rawType, &rawValue, &rawTraceback);
    PyErr_NormalizeException(&rawType, &rawValue, &rawTraceback);
    PyObjectPtr type(rawType);
    PyObjectPtr value(rawValue);
    PyObjectPtr traceback(rawTraceback);

    PyObjectPtr tracebackModule(PyImport_ImportModule("traceback"));
    PyObjectPtr formatter(tracebackModule
                              ? PyObject_GetAttrString(tracebackModule.get(), "format_exception")
                              : nullptr);
    PyObjectPtr formatted;
    if (formatter && PyCallable_Check(formatter.get())) {
        formatted = PyObjectPtr(PyObject_CallFunctionObjArgs(formatter.get(),
                                                             type ? type.get() : Py_None,
                                                             value ? value.get() : Py_None,
                                                             traceback ? traceback.get() : Py_None,
                                                             nullptr));
    }

    PyObjectPtr separator(PyUnicode_FromString(""));
    if (PyObjectPtr joined(formatted && separator ? PyUnicode_Join(separator.get(), formatted.get())
                                                  : nullptr);
        joined) {
        const char *text = PyUnicode_AsUTF8(joined.get());
        if (text != nullptr)
            return text;
    }

    PyErr_Clear();
    PyObjectPtr fallback(value ? PyObject_Str(value.get()) : nullptr);
    const char *text = fallback ? PyUnicode_AsUTF8(fallback.get()) : nullptr;
    return text == nullptr ? "Unknown Python exception" : text;
}

std::filesystem::path packagedPythonPath() {
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void *>(&packagedPythonPath), &info) == 0 ||
        info.dli_fname == nullptr) {
        return {};
    }
    const auto libraryDirectory = std::filesystem::path(info.dli_fname).parent_path();
    const std::array<std::filesystem::path, 2> candidates = {
        libraryDirectory / "../../share/opk/python",
        libraryDirectory / "../../../share/opk/python",
    };
    for (const auto &candidate : candidates) {
        if (std::filesystem::is_directory(candidate))
            return candidate;
    }
    return candidates[0];
}

std::filesystem::path installedPythonRuntimePath() {
    return OPK_INSTALLED_PYTHON_PATH;
}

} // namespace opk::python
