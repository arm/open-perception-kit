/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "op/Op.h"
#include "op/OpChainContext.h"
#include "pek/Result.h"

#include "pek/TensorParser.h"

namespace pek::stdop {

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
class GenericPostprocessOp : public pek::op::Op {
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
    virtual pek::Result<void> configure(const pek::AttributeMap &attributes) override;
    /**
     * @brief Executes postprocessing: parses tensors and populates Perception.
     *
     * Calls the TensorParser with inference output tensors from OpChainContext,
     * which populates OpChainContext.perception with detection/classification results.
     *
     * @param opChainContext Context containing output tensors and Perception object.
     * @return Continue after successful parsing, or a parsing error.
     */
    virtual pek::Result<pek::op::OpSignal>
    process(pek::op::OpChainContext &opChainContext) override;
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
    virtual pek::Result<void> bind(size_t index, const std::vector<pek::op::Op *> &ops) override;

  private:
    std::unique_ptr<pek::TensorParser> parser;
    pek::AttributeMap attributes;
};

} // namespace pek::stdop
