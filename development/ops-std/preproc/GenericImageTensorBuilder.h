/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

#pragma once

#include "opk/Result.h"
#include "opk/TensorBuilder.h"

namespace opk::stdop::preproc {

/**
 * @brief Generic image tensor builder that dispatches conversion kernels by source/destination
 * layout and type.
 */
struct GenericImageTensorBuilder : public opk::TensorBuilder {

    /**
     * @brief Builds destination image tensor content from the source image descriptor.
     * @param setup Source and destination image layout descriptors used for conversion.
     * @return Success on supported conversion; error when kind/type combination is unsupported.
     */
    opk::Result<void> build(const TensorBuilder::Setup &setup) override;
};

} // namespace opk::stdop::preproc
