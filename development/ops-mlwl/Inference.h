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

#include "opk/Model.h"
#include "opk/ModelDescriptor.h"
#include "opk/Result.h"

#include <memory>

namespace opk::vulkan {
struct Input;
}

namespace opk::mlwl {

class Inference {
  public:
    explicit Inference(bool vulkanInput = false);
    ~Inference();

    opk::Result<void> setupFromJson(const std::string &filePath);
    opk::Result<void> setup(const opk::ModelDescriptor &descriptor);
    bool isReady() const;
    opk::Result<void> inference();

    const opk::Model &getModel() const;
    uint8_t *getInputTensorDataAddress(size_t index);
    std::shared_ptr<const opk::vulkan::Input> getVulkanInput(size_t index) const;
    const uint8_t *getOutputTensorDataAddress(size_t index) const;
    opk::Shape getOutputTensorFinalShape(size_t index) const;

  private:
    struct State;
    const bool vulkanInput;
    std::shared_ptr<State> state;
};

} // namespace opk::mlwl
