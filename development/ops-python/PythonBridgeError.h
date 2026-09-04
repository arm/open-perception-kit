/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <stdexcept>

namespace pek::python {

class PythonBridgeError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

} // namespace pek::python
