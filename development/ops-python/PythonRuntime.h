/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <Python.h>

#include <filesystem>
#include <string>
#include <vector>

namespace opk::python {

void ensureRuntime();
std::string formatPythonError();

class GILGuard {
  public:
    GILGuard() = default;
    GILGuard(const GILGuard &) = delete;
    GILGuard &operator=(const GILGuard &) = delete;
    ~GILGuard();

  private:
    PyGILState_STATE state = PyGILState_Ensure();
};

class PyObjectPtr {
  public:
    explicit PyObjectPtr(PyObject *object = nullptr) noexcept;
    PyObjectPtr(const PyObjectPtr &) = delete;
    PyObjectPtr &operator=(const PyObjectPtr &) = delete;
    PyObjectPtr(PyObjectPtr &&other) noexcept;
    PyObjectPtr &operator=(PyObjectPtr &&other) noexcept;
    ~PyObjectPtr();

    [[nodiscard]] PyObject *get() const noexcept;
    [[nodiscard]] PyObject *release() noexcept;
    [[nodiscard]] explicit operator bool() const noexcept;

  private:
    PyObject *object;
};

class PythonPathTemplate {
  public:
    PythonPathTemplate() = default;
    explicit PythonPathTemplate(const std::vector<std::filesystem::path> &paths);
    PythonPathTemplate(const PythonPathTemplate &) = delete;
    PythonPathTemplate &operator=(const PythonPathTemplate &) = delete;
    PythonPathTemplate(PythonPathTemplate &&) noexcept = default;
    PythonPathTemplate &operator=(PythonPathTemplate &&) noexcept = default;
    void release() noexcept;

  private:
    friend class PythonPathGuard;

    PyObjectPtr sysModule;
    PyObjectPtr path;
};

class PythonPathGuard {
  public:
    explicit PythonPathGuard(const PythonPathTemplate &pathTemplate);
    PythonPathGuard(const PythonPathGuard &) = delete;
    PythonPathGuard &operator=(const PythonPathGuard &) = delete;
    ~PythonPathGuard();

  private:
    PyObject *sysModule = nullptr;
    PyObjectPtr originalPathObject;
    PyObjectPtr temporaryPath;
};

std::filesystem::path packagedPythonPath();

} // namespace opk::python
