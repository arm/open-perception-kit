/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 *************************************************************/

// Generated file. Do not edit.
// SDK users: change schemas or generator inputs, then regenerate this file.

#pragma once

#include <Python.h>

#include "open_perception_kit.h"

namespace open_perception_kit::python_bridge {

void append_inittab();
[[nodiscard]] bool initialize_module();

[[nodiscard]] PyObject *wrap(container::envelope &envelope);
bool invalidate(PyObject *object) noexcept;

class scoped_envelope {
  public:
    explicit scoped_envelope(container::envelope &envelope);
    scoped_envelope(const scoped_envelope &) = delete;
    scoped_envelope &operator=(const scoped_envelope &) = delete;
    scoped_envelope(scoped_envelope &&other) noexcept;
    scoped_envelope &operator=(scoped_envelope &&other) noexcept;
    ~scoped_envelope();

    [[nodiscard]] PyObject *py_object() const noexcept;

  private:
    PyObject *object_ = nullptr;
};

} // namespace open_perception_kit::python_bridge
