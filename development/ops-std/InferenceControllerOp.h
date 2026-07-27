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
 * 1. The controller populates OpChainContext.inferenceImageCrops with regions to infer
 * 2. Preprocess, inference, and postprocess operations consume one crop per loop iteration
 * 3. GenericImagePreprocessOp returns OpSignal::BreakLoop when no crops remain
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
     * @param stopToken Cooperative cancellation token for this setup attempt.
     * @return Result indicating success or configuration error.
     */
    pek::Result<void> configure(const pek::AttributeMap &attributes,
                                std::stop_token stopToken) override;
    /**
     * @brief Populates crop state for the following inference loop workers.
     *
     * Populates inference crop state for the following loop workers.
     * Loop transitions are controlled by OpSignal values returned from process().
     *
     * @param opChainContext Context for shared inference state.
     * @return Continue after populating crop state, or a processing error.
     */
    pek::Result<pek::op::OpSignal> process(pek::op::OpChainContext &opChainContext) override;
    /**
     * @brief Resolves references to other operations if needed for inference control.
     *
     * @param index Position of this operation in the OpChain.
     * @param ops Vector of all operations in the chain.
     * @return Result indicating success or binding error.
     */
    pek::Result<void> bind(size_t index, const std::vector<pek::op::Op *> &ops) override;

  private:
    std::string contentType;
};

} // namespace pek::stdop
