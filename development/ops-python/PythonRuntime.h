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

#pragma once

#include <Python.h>

#include <filesystem>
#include <string>
#include <vector>

namespace opk::python {

void ensureRuntime(const std::vector<std::filesystem::path> &pythonPaths = {});
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
