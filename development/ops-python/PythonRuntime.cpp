/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "PythonRuntime.h"

#include <dlfcn.h>

#include <mutex>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "TensorBridge.h"
#include "python_bridge/perception_python_bridge.h"

#ifndef PEK_PYTHON_EXECUTABLE
#define PEK_PYTHON_EXECUTABLE ""
#endif

#ifndef PEK_DEVELOPMENT_PYTHON_PATH
#define PEK_DEVELOPMENT_PYTHON_PATH ""
#endif

namespace pek::python {
namespace {

std::once_flag initializationFlag;
void *pythonLibraryHandle = nullptr;

void exposePythonSymbols() {
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void *>(&Py_InitializeFromConfig), &info) == 0 ||
        info.dli_fname == nullptr) {
        throw std::runtime_error("Failed to locate the embedded Python library");
    }

    pythonLibraryHandle = dlopen(
        info.dli_fname, RTLD_NOW | RTLD_GLOBAL | RTLD_NODELETE); // NOLINT(concurrency-mt-unsafe)
    if (pythonLibraryHandle == nullptr) {
        const char *error = dlerror();
        throw std::runtime_error(std::string("Failed to expose embedded Python symbols: ") +
                                 (error == nullptr ? "unknown error" : error));
    }
}

void initializeRuntime() {
    exposePythonSymbols();
    perception::python_bridge::append_inittab();
    appendTensorModuleInittab();

    PyConfig config;
    PyConfig_InitPythonConfig(&config);
    PyStatus status = PyStatus_Ok();
    if (std::string_view(PEK_PYTHON_EXECUTABLE).empty() == false) {
        status = PyConfig_SetBytesString(&config, &config.program_name, PEK_PYTHON_EXECUTABLE);
    }
    if (!PyStatus_Exception(status))
        status = Py_InitializeFromConfig(&config);

    const std::string error = status.err_msg == nullptr ? "unknown error" : status.err_msg;
    PyConfig_Clear(&config);
    if (PyStatus_Exception(status) || !Py_IsInitialized())
        throw std::runtime_error("Failed to initialize embedded Python: " + error);

    PyEval_SaveThread();
}

} // namespace

void ensureRuntime() {
    std::call_once(initializationFlag, initializeRuntime);
}

GILGuard::GILGuard() : state(PyGILState_Ensure()) {}

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

PythonPathGuard::PythonPathGuard(const std::vector<std::filesystem::path> &paths) {
    sysModule = PyObjectPtr(PyImport_ImportModule("sys"));
    originalPathObject =
        PyObjectPtr(sysModule ? PyObject_GetAttrString(sysModule.get(), "path") : nullptr);
    originalPathSnapshot =
        PyObjectPtr(originalPathObject ? PySequence_List(originalPathObject.get()) : nullptr);
    if (!originalPathObject || !PyList_Check(originalPathObject.get()) || !originalPathSnapshot)
        throw std::runtime_error("Failed to access Python sys.path: " + formatPythonError());

    for (auto iterator = paths.rbegin(); iterator != paths.rend(); ++iterator) {
        PyObjectPtr value(PyUnicode_FromString(iterator->string().c_str()));
        if (!value || PyList_Insert(originalPathObject.get(), 0, value.get()) < 0) {
            const std::string error = formatPythonError();
            if (PyList_SetSlice(originalPathObject.get(),
                                0,
                                PyList_Size(originalPathObject.get()),
                                originalPathSnapshot.get()) < 0)
                PyErr_Clear();
            if (PyObject_SetAttrString(sysModule.get(), "path", originalPathObject.get()) < 0)
                PyErr_Clear();
            throw std::runtime_error("Failed to update Python sys.path: " + error);
        }
    }
}

PythonPathGuard::~PythonPathGuard() {
    if (!sysModule || !originalPathObject || !originalPathSnapshot)
        return;
    if (PyList_SetSlice(originalPathObject.get(),
                        0,
                        PyList_Size(originalPathObject.get()),
                        originalPathSnapshot.get()) < 0)
        PyErr_Clear();
    if (PyObject_SetAttrString(sysModule.get(), "path", originalPathObject.get()) < 0)
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
    return std::filesystem::path(info.dli_fname).parent_path() / "../../share/pek/python";
}

} // namespace pek::python
