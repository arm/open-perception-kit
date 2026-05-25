/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/ImageOpDesc.h"
#include "pek/Result.h"

namespace pek {

/**
 * @brief Interface for preparing input tensors from image data.
 */
struct TensorBuilder {

    /**
     * @brief Builder input configuration.
     */
    struct Setup {
        /// Source image descriptor (incoming layout/normalization context).
        ImageOpDesc imageSourceDesc;
        /// Destination tensor/image descriptor (target layout/normalization context).
        ImageOpDesc imageDestinationDesc;
    };

    /**
     * @brief Virtual destructor for polymorphic use.
     */
    virtual ~TensorBuilder() = default;

    /**
     * @brief Builds tensors according to the provided setup.
     * @param setup Source and destination image/tensor description.
     * @return Success or error.
     */
    virtual pek::Result<void> build(const TensorBuilder::Setup &setup) = 0;
};

} // namespace pek