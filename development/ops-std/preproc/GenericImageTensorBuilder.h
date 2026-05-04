/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "amp/Result.h"
#include "amp/TensorBuilder.h"

namespace amp {

/**
 * @brief Generic image tensor builder that dispatches conversion kernels by source/destination layout and type.
 */
struct GenericImageTensorBuilder : public amp::TensorBuilder {

    /**
     * @brief Builds destination image tensor content from the source image descriptor.
     * @param setup Source and destination image layout descriptors used for conversion.
     * @return Success on supported conversion; error when kind/type combination is unsupported.
     */
    virtual amp::Result<void> build(const TensorBuilder::Setup &setup) override;
};

} // namespace amp
