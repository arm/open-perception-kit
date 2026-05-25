/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "op/Op.h"
#include "op/OpChainContext.h"
#include "pek/Result.h"

namespace pek::stdop {

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
 * 1. Preprocessing operations populate OpChainContext.inferenceImageCrops with regions to infer
 * 2. Inference operation executes on these crops
 * 3. This controller signals loop completion by setting OpChainContext.breakLoop after inference
 */
class InferenceControllerOp : public pek::op::Op {
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
    virtual pek::Result<void> configure(const pek::AttributeMap &attributes) override;
    /**
     * @brief Executes loop control logic: starts inference or signals completion.
     *
     * Manages OpChainContext.breakLoop and loop state transitions.
     * When all inference crops have been processed, sets breakLoop to true.
     *
     * @param opChainContext Context for loop control state.
     * @return Result indicating success or processing error.
     */
    virtual pek::Result<void> process(pek::op::OpChainContext &opChainContext) override;
    /**
     * @brief Resolves references to other operations if needed for inference control.
     *
     * @param index Position of this operation in the OpChain.
     * @param ops Vector of all operations in the chain.
     * @return Result indicating success or binding error.
     */
    virtual pek::Result<void> bind(size_t index, const std::vector<pek::op::Op *> &ops) override;

  private:
    std::string contentType;
};

} // namespace pek::stdop
