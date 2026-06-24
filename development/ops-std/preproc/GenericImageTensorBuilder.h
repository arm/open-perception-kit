/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Result.h"
#include "pek/TensorBuilder.h"

namespace pek::stdop::preproc {

/**
 * @brief Generic image tensor builder that dispatches conversion kernels by source/destination
 * layout and type.
 */
struct GenericImageTensorBuilder : public pek::TensorBuilder {

    /**
     * @brief Builds destination image tensor content from the source image descriptor.
     * @param setup Source and destination image layout descriptors used for conversion.
     * @return Success on supported conversion; error when kind/type combination is unsupported.
     */
    pek::Result<void> build(const TensorBuilder::Setup &setup) override;
};

} // namespace pek::stdop::preproc
