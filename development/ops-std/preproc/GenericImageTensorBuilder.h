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
