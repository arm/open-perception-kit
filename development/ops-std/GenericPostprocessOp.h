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

#include "opk/TensorParser.h"

namespace opk::stdop {

/**
 * @brief Generic postprocessing operation for tensor parsing.
 *
 * GenericPostprocessOp is an Op that reads inference output tensors, routes them to
 * the appropriate model-specific parser, and populates the OpChainContext's Perception
 * object with structured results (detections, classifications, segmentations, etc.).
 *
 * **Configuration Attributes:**
 * - `parserType`: Identifier of the parser to use (e.g., "yolo", "mobilenet", "modnet")
 * - Additional parser-specific attributes are passed through to the parser
 *
 * **Typical Usage:**
 * 1. Upstream inference operation executes inference and stores tensor outputs
 * 2. This operation receives output tensor references from OpChainContext
 * 3. Parser converts tensors to Perception result objects
 * 4. Results stored in OpChainContext.perception for final output
 */
class GenericPostprocessOp : public opk::op::Op, public opk::op::OpInterfacePostprocessor {
  public:
    /**
     * @brief Constructs a generic postprocessing operation.
     */
    GenericPostprocessOp();
    /**
     * @brief Destroys the postprocessing operation and releases parser resources.
     */
    virtual ~GenericPostprocessOp();

    /**
     * @brief Configures the operation with parser type and model-specific attributes.
     *
     * Reads attributes: parserType, and any model-specific attributes.
     * Instantiates the appropriate TensorParser at configure time.
     *
     * @param attributes Configuration map from OpChainDescriptor.
     * @return Result indicating success or parsing error (unsupported parser type, etc.).
     */
    opk::Result<void> configure(const opk::AttributeMap &attributes) override;
    /**
     * @brief Executes postprocessing: parses tensors and populates Perception.
     *
     * Calls the TensorParser with inference output tensors from OpChainContext,
     * which populates OpChainContext.perception with detection/classification results.
     *
     * @param opChainContext Context containing output tensors and Perception object.
     * @return Continue after successful parsing, or a parsing error.
     */
    opk::Result<opk::op::OpSignal> process(opk::op::OpChainContext &opChainContext) override;
    /**
     * @brief Resolves upstream inference operation for model metadata.
     *
     * May cache references to the upstream inference Op for quick access during
     * process() calls.
     *
     * @param index Position of this operation in the OpChain.
     * @param ops Vector of all operations in the chain.
     * @return Result indicating success or binding error.
     */
    opk::Result<void> bind(size_t index, const std::vector<opk::op::Op *> &ops) override;
    std::vector<std::string_view> getProvidedContentTypes() const override;

  private:
    std::unique_ptr<opk::TensorParser> parser;
    opk::AttributeMap attributes;
    std::string parserName;
};

} // namespace opk::stdop
