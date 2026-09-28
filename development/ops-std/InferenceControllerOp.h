/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

namespace opk::stdop {

/**
 * @brief Control operation for managing inference loop execution.
 *
 * InferenceControllerOp is an Op that orchestrates the inference loop lifecycle.
 * It manages when inference begins, processes the inference output, and signals
 * when to conclude the loop. Typically placed immediately after preprocessing
 * and acts as a sentinel for loop control.
 *
 * **Configuration Attributes:**
 * - `contentType`: Content type descriptor for the inferred data (e.g., "image", "video")
 *
 * **Typical Usage:**
 * 1. The controller populates OpChainContext.inferenceImageCrops with regions to infer
 * 2. Preprocess, inference, and postprocess operations consume one crop per loop iteration
 * 3. GenericImagePreprocessOp returns OpSignal::BreakLoop when no crops remain
 */
class InferenceControllerOp : public opk::op::Op, public opk::op::OpInterfaceContentConsumer {
  public:
    /**
     * @brief Constructs an inference controller operation.
     */
    InferenceControllerOp();
    /**
     * @brief Destroys the inference controller operation.
     */
    virtual ~InferenceControllerOp();

    /**
     * @brief Configures the operation with content type metadata.
     *
     * Reads attributes: contentType for content-specific inference control.
     *
     * @param attributes Configuration map from OpChainDescriptor.
     * @return Result indicating success or configuration error.
     */
    opk::Result<void> configure(const opk::AttributeMap &attributes) override;
    /**
     * @brief Populates crop state for the following inference loop workers.
     *
     * Populates inference crop state for the following loop workers.
     * Loop transitions are controlled by OpSignal values returned from process().
     *
     * @param opChainContext Context for shared inference state.
     * @return Continue after populating crop state, or a processing error.
     */
    opk::Result<opk::op::OpSignal> process(opk::op::OpChainContext &opChainContext) override;
    /**
     * @brief Resolves references to other operations if needed for inference control.
     *
     * @param index Position of this operation in the OpChain.
     * @param ops Vector of all operations in the chain.
     * @return Result indicating success or binding error.
     */
    opk::Result<void> bind(size_t index, const std::vector<opk::op::Op *> &ops) override;
    std::vector<std::string_view> getRequiredContentTypes() const override;

  private:
    std::string contentType;
};

} // namespace opk::stdop
