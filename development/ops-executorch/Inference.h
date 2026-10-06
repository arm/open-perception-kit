/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <executorch/extension/module/module.h>
#include <executorch/extension/tensor/tensor_ptr.h>
#include <executorch/runtime/core/evalue.h>

#include "opk/Model.h"
#include "opk/ModelDescriptor.h"
#include "opk/Result.h"

#include <memory>
#include <vector>

namespace opk::extrch {

struct Inference {

    Inference();
    virtual ~Inference();

    opk::Result<void> setupFromJson(const std::string &filePath);
    opk::Result<void> setup(const opk::ModelDescriptor &modelDesc);

    // Runs one forward pass using the current OPK-owned input buffers.
    bool isReady() const {
        return setupReady;
    }

    opk::Result<void> inference();

    // Generic preprocess/postprocess code uses this model view.
    const opk::Model &getModel() const {
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

    opk::Shape getOutputTensorFinalShape(size_t index) const {
        return outputTensorFinalShapes[index];
    }

  private:
    std::unique_ptr<executorch::extension::Module> module;
    opk::ModelDescriptor modelDescriptor;
    static opk::Result<opk::Model> inspectModel(executorch::extension::Module &module);

    std::string modelPath;
    bool setupReady = false;
    bool useDynamicOutput = false;

    opk::Model model;

    void setTensorSizes();
    // OPK owns input tensor memory; ExecuTorch receives non-owning tensor views over it.
    std::vector<std::vector<uint8_t>> inputTensors;

    // Keeps ExecuTorch output EValues alive while downstream TensorViews read their data.
    std::vector<executorch::runtime::EValue> lastOutputs;
    const uint8_t *outputTensorPointers[opk::MaxTensorCount] = {nullptr};
    opk::Shape outputTensorFinalShapes[opk::MaxTensorCount];
};
} // namespace opk::extrch
