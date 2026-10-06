/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

#include "opk/ImageOpDesc.h"
#include "opk/Result.h"

namespace opk {

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
    virtual opk::Result<void> build(const TensorBuilder::Setup &setup) = 0;
};

} // namespace opk