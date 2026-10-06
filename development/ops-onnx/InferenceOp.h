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

#include "op/Op.h"
#include "op/OpChainContext.h"
#include "opk/Result.h"

#include "Inference.h"
#include <memory>

namespace opk::onnx {

class InferenceOp : public opk::op::Op, public opk::op::OpInterfaceInference {
  public:
    InferenceOp();
    virtual ~InferenceOp();

    const opk::Model &getModel() const override;
    uint8_t *getTensorDataAddress(size_t index) const override;

    opk::Result<void> configure(const opk::AttributeMap &attributes) override;
    opk::Result<void> bind(size_t index, const std::vector<opk::op::Op *> &ops) override;
    opk::Result<opk::op::OpSignal> process(opk::op::OpChainContext &opChainContext) override;

  private:
    std::unique_ptr<opk::onnx::Inference> inference;
};

} // namespace opk::onnx
