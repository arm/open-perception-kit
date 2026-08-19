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

namespace pek::python {

class LoadedScript;

class PythonScriptOp final : public pek::op::Op {
  public:
    PythonScriptOp();
    ~PythonScriptOp() override;

    pek::Result<void> configure(const pek::AttributeMap &attributes) override;
    pek::Result<void> bind(size_t index, const std::vector<pek::op::Op *> &ops) override;
    pek::Result<pek::op::OpSignal> process(pek::op::OpChainContext &context) override;

  private:
    std::filesystem::path scriptPath;
    std::vector<std::filesystem::path> pythonPaths;
    std::unique_ptr<LoadedScript> loadedScript;
    const pek::Model *model = nullptr;
};

} // namespace pek::python
