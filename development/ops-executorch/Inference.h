/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <executorch/extension/module/module.h>
#include <executorch/extension/tensor/tensor_ptr.h>
#include <executorch/runtime/core/evalue.h>

#include "pek/Model.h"
#include "pek/ModelDescriptor.h"
#include "pek/Result.h"

#include <memory>
#include <vector>

namespace pek::extrch {

struct Inference {

    Inference();
    virtual ~Inference();

    pek::Result<void> setup(const pek::ModelDescriptor &modelDesc);

    // Runs one forward pass using the current PEK-owned input buffers.
    bool isReady() const {
        return setupReady;
    }

    pek::Result<void> inference();

    // Generic preprocess/postprocess code uses this model view.
    const pek::Model &getModel() const {
        return model;
    }

    // Preprocess writes directly into this buffer before inference().
    uint8_t *getInputTensorDataAddress(size_t index) {
        return inputTensors[index].data();
    }

    // Valid after inference(), until the next inference() call.
    const uint8_t *getOutputTensorDataAddress(size_t index) const {
        assert(outputTensorPointers[index]);
        return outputTensorPointers[index];
    }

    pek::Shape getOutputTensorFinalShape(size_t index) const {
        return outputTensorFinalShapes[index];
    }

  private:
    std::unique_ptr<executorch::extension::Module> module;
    pek::ModelDescriptor modelDescriptor;
    static pek::Result<pek::Model> inspectModel(executorch::extension::Module &module);

    std::string modelPath, modelFamily;
    bool setupReady = false;
    bool useDynamicOutput = false;

    pek::Model model;

    void setTensorSizes();
    // PEK owns input tensor memory; ExecuTorch receives non-owning tensor views over it.
    std::vector<std::vector<uint8_t>> inputTensors;

    // Keeps ExecuTorch output EValues alive while downstream TensorViews read their data.
    std::vector<executorch::runtime::EValue> lastOutputs;
    const uint8_t *outputTensorPointers[pek::MaxTensorCount] = {nullptr};
    pek::Shape outputTensorFinalShapes[pek::MaxTensorCount];
};
} // namespace pek::extrch
