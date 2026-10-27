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

#include "op/Op.h"

namespace opk::stdop {

/**
 * @brief PaddleOCR preprocessing from BGRA to BGR float32 NCHW.
 *
 * Required mode is "detection" (centered aspect fit) or "recognition"
 * (height fit, width compression, right padding). Required paddingValue is normalized.
 * The next inference Op's ModelInput supplies canvas dimensions and BGR mean/std.
 * Optional inputImageSourceName defaults to pipelineVideoFrame; optional
 * inputImageTensorIndex defaults to zero. The next Op must provide inference.
 */
class PaddleOcrPreprocessOp : public opk::op::Op {
  public:
    opk::Result<void> configure(const opk::AttributeMap &attributes) override;
    opk::Result<void> bind(size_t index, const std::vector<opk::op::Op *> &ops) override;
    opk::Result<opk::op::OpSignal> process(opk::op::OpChainContext &context) override;

  private:
    enum class Mode { Unconfigured, Detection, Recognition };
    Mode mode = Mode::Unconfigured;
    float paddingValue = 0.0f;
    std::string inputImageSourceName = "pipelineVideoFrame";
    size_t inputImageTensorIndex = 0;
    const opk::op::OpInterfaceInference *inferenceOp = nullptr;
    opk::Model model;
};

} // namespace opk::stdop
