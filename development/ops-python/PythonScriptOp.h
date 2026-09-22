/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <Python.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "op/Op.h"

namespace opk::python {

class LoadedScript;

class PythonScriptOp final : public opk::op::Op {
  public:
    PythonScriptOp();
    ~PythonScriptOp() override;

    opk::Result<void> configure(const opk::AttributeMap &attributes) override;
    opk::Result<void> bind(size_t index, const std::vector<opk::op::Op *> &ops) override;
    opk::Result<opk::op::OpSignal> process(opk::op::OpChainContext &context) override;

  private:
    std::filesystem::path scriptPath;
    std::unique_ptr<LoadedScript> loadedScript;
    const opk::Model *model = nullptr;
};

} // namespace opk::python
