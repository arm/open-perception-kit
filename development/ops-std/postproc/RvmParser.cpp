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

#include "postproc/RvmParser.h"
#include "opk/TensorParser.h"
#include "opk/Types.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fmt/core.h>
#include <memory>
#include <string>
#include <utility>

using namespace opk;
using namespace opk::stdop::postproc;

opk::Result<void> RvmParser::parse(const opk::TensorParser::Input &input,
                                   open_perception_kit::FrameResults &results) {

    if (!input.tensors[0] || !input.tensors[1]) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData, "RvmParser: input tensors are null"));
    }

    const auto shape = input.tensors[1]->getShape();
    if (shape.rank != 4) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData,
                      fmt::format("RvmParser: expected 4D tensor, got {}D", shape.rank)));
    }
    if (shape.dims[0] != 1) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData,
                      fmt::format("RvmParser: batch size must be 1, got {}", shape.dims[0])));
    }
    if (shape.dims[1] != 1) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData,
                      fmt::format("RvmParser: expected 1 channel, got {}", shape.dims[1])));
    }

    if (shape.dims[2] <= 0 || shape.dims[3] <= 0 || !input.tensors[1]->isValid()) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData, "RvmParser: invalid alpha dimensions or buffer"));
    }

    const size_t maskHeight = shape.dims[2];
    const size_t maskWidth = shape.dims[3];

    opk::Bitmap bitmap(opk::Bitmap::Type::Uint8, maskWidth, maskHeight);

    auto *dst = bitmap.getMutableData();

    for (size_t i = 0; i < maskHeight * maskWidth; ++i) {
        const float alpha = input.tensors[1]->get(i);
        if (!std::isfinite(alpha)) {
            return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData, "RvmParser: non-finite alpha"));
        }
        // OPK masks describe background opacity, the inverse of RVM foreground alpha.
        // Preserve soft edges; consumers can threshold the bytes when needed.
        dst[i] = static_cast<uint8_t>(std::lround((1.0f - std::clamp(alpha, 0.0f, 1.0f)) * 255.0f));
    }

    auto mask = std::make_unique<open_perception_kit::metadata::SegmentationMaskT>();
    mask->object = open_perception_kit::makeObjectMeta(0U, input.inferenceInfo.parentId);
    mask->bitmap = open_perception_kit::makeBitmapData(bitmap);

    open_perception_kit::metadata::SegmentationMasksT payload;
    payload.layer =
        open_perception_kit::makeLayerInfo({.model = input.inferenceInfo.modelName,
                                            .inferElementId = input.inferenceInfo.inferElementId,
                                            .contentType = k_content_type,
                                            .compositingMode = "backgroundReplacement",
                                            .producer = &input.producerInfo});
    payload.masks.push_back(std::move(mask));
    results.add(std::move(payload));

    return {};
}
