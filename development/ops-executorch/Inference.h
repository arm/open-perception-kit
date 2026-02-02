#pragma once

#include <cstdint>
#include <executorch/extension/module/module.h>

#include "ModelDescriptor.h"
#include "amp/Model.h"
#include "amp/Result.h"

#include <memory>
#include <vector>

namespace exct {

struct Inference {

    Inference();
    virtual ~Inference();

    amp::Result<void> setupFromJson(const std::string &filePath);
    amp::Result<void> setup(const ModelDescriptor &modelDesc);

  private:
    std::unique_ptr<executorch::extension::Module> module;
    ModelDescriptor modelDescriptor;
    static amp::Result<amp::Model> inspectModel(executorch::extension::Module &module);

    std::string modelPath, modelFamily;
    bool setupReady = false;
    bool useDynamicOutput = false;

    amp::Model model;

    void setTensorSizes();
    std::vector<std::vector<uint8_t>> inputTensors;
    std::vector<std::vector<uint8_t>> outputTensors;

    void Forward();
};
} // namespace exct
