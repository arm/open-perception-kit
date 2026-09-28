/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 *************************************************************/

#pragma once

#include <stdexcept>

namespace opk::python {

class PythonBridgeError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

} // namespace opk::python
