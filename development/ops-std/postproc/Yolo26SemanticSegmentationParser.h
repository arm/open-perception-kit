/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

#include "opk/TensorParser.h"

namespace opk::stdop::postproc {

/**
 * @brief YOLO26n-sem full-frame Cityscapes segmentation parser.
 *
 * Expects a Uint8 class map [1,1024,1024] with IDs 0 through 18.
 * Produces one source-sized class-ID bitmap after removing recorded letterboxing.
 */
struct Yolo26SemanticSegmentationParser : public opk::TensorParser {

    std::vector<opk::OutputCapability> getProvidedOutputs() const override {
        return {opk::outputCapability<open_perception_kit::metadata::SemanticSegmentationMasksT>()};
    }

    opk::Result<void> parse(const Input &input,
                            open_perception_kit::FrameResults &results) override;
};

} // namespace opk::stdop::postproc
