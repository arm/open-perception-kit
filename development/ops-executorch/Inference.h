/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <cstdint>
#include <executorch/extension/module/module.h>

#include "pek/Model.h"
#include "pek/ModelDescriptor.h"
#include "pek/Result.h"

#include <memory>
#include <vector>

namespace exct {

struct Inference {

    Inference();
    virtual ~Inference();

    pek::Result<void> setupFromJson(const std::string &filePath);
    pek::Result<void> setup(const ModelDescriptor &modelDesc);

  private:
    std::unique_ptr<executorch::extension::Module> module;
    ModelDescriptor modelDescriptor;
    static pek::Result<pek::Model> inspectModel(executorch::extension::Module &module);

    std::string modelPath, modelFamily;
    bool setupReady = false;
    bool useDynamicOutput = false;

    pek::Model model;

    void setTensorSizes();
    std::vector<std::vector<uint8_t>> inputTensors;
    std::vector<std::vector<uint8_t>> outputTensors;

    void Forward();
};
} // namespace exct
