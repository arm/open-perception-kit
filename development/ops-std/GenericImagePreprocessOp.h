/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "op/Op.h"
#include "op/OpChainContext.h"
#include "pek/Model.h"
#include "pek/Result.h"
#include "pek/Types.h"

#include "preproc/GenericImageTensorBuilder.h"
#include <cstdint>

namespace pek::stdop {

/**
 * @brief Generic image preprocessing operation for tensor conversion.
 *
 * GenericImagePreprocessOp is an Op that extracts image data from the OpChainContext,
 * converts it to inference input tensor format, and prepares tensor addresses for the
 * downstream inference operation. It uses GenericImageTensorBuilder to dispatch format
 * conversions (e.g., BGRA8 HWC to RGB float32 CHW).
 *
 * **Configuration Attributes:**
 * - `inputImageSourceName`: Name of the bitmap in OpChainContext.bitmapViews (e.g., "frame")
 * - `inputImageTensorIndex`: Index of the tensor in the inference model to populate
 *
 * **Typical Usage:**
 * 1. Upstream preprocessing operations populate OpChainContext.bitmapViews["frame"] with BGRA data
 * 2. This operation converts it to the model's input format
 * 3. Downstream inference operation uses the prepared tensor data
 */
class GenericImagePreprocessOp : public pek::op::Op {
  public:
    /**
     * @brief Constructs a generic image preprocessing operation.
     */
    GenericImagePreprocessOp();
    /**
     * @brief Destroys the preprocessing operation and releases resources.
     */
    virtual ~GenericImagePreprocessOp();

    /**
     * @brief Configures the operation with image source and tensor index.
     *
     * Reads attributes: inputImageSourceName, inputImageTensorIndex.
     *
     * @param attributes Configuration map from OpChainDescriptor.
     * @return Result indicating success or configuration error.
     */
    virtual pek::Result<void> configure(const pek::AttributeMap &attributes) override;
    /**
     * @brief Executes preprocessing: reads bitmap, converts format, prepares tensor data.
     *
     * Reads image from OpChainContext.bitmapViews[inputImageSourceName],
     * converts to model input format using GenericImageTensorBuilder,
     * and stores tensor pointers for use by downstream inference.
     *
     * @param opChainContext Context containing input image and tensor setup.
     * @return Result indicating success or preprocessing error.
     */
    virtual pek::Result<void> process(pek::op::OpChainContext &opChainContext) override;
    /**
     * @brief Resolves the upstream inference operation to get model information.
     *
     * Finds the inference Op in the chain (typically implementing OpInterfaceInference)
     * to extract model and tensor metadata.
     *
     * @param index Position of this operation in the OpChain.
     * @param ops Vector of all operations in the chain.
     * @return Result indicating success or binding error.
     */
    virtual pek::Result<void> bind(size_t index, const std::vector<pek::op::Op *> &ops) override;

  private:
    std::string inputImageSourceName;
    size_t inputImageTensorIndex;

    pek::stdop::preproc::GenericImageTensorBuilder genericImageInputTensorBuilder;

    pek::Model upcomingInferenceModel;
    uint8_t *upcomingTensorAddresses[pek::MaxTensorCount] = {nullptr};
};

} // namespace pek::stdop
