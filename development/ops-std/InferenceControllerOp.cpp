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

#include "InferenceControllerOp.h"

#include <algorithm>
#include <fmt/core.h>
#include <memory>
#include <utility>

#include "opk/FrameResults.h"
#include "opk/Result.h"
#include "opk/Types.h"
#include "tl/expected.hpp"

using namespace opk::stdop;

InferenceControllerOp::InferenceControllerOp() = default;
InferenceControllerOp::~InferenceControllerOp() = default;

opk::Result<void> InferenceControllerOp::bind(size_t index, const std::vector<opk::op::Op *> &ops) {
    return {};
}

opk::Result<void> InferenceControllerOp::configure(const opk::AttributeMap &attributes) {
    contentType = attributes.getStringOrDefault("contentType", "");
    return {};
}

std::vector<std::string_view> InferenceControllerOp::getRequiredContentTypes() const {
    return contentType.empty() ? std::vector<std::string_view>{}
                               : std::vector<std::string_view>{contentType};
}

opk::Result<opk::op::OpSignal>
InferenceControllerOp::process(opk::op::OpChainContext &opChainContext) {
    auto *pipelineVideoFrame = opChainContext.getVideoFrame("pipelineVideoFrame");

    // TODO: later it can be also audio data not video only
    if (pipelineVideoFrame == nullptr) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                      "InferenceControllerOp needs pipelineVideoFrame VideoFrame"));
    }

    opChainContext.inferenceInfo.modelName.clear();

    if (contentType.empty()) {
        const auto producer = producerInfo(
            opChainContext.inferenceInfo.inferElementId, opName, "opk-std-ops/InferenceController");
        const auto frameId = opChainContext.videoFrameId("pipelineVideoFrame", producer);

        opk::PixelRect rect;
        rect.x = 0;
        rect.y = 0;
        rect.width = pipelineVideoFrame->width();
        rect.height = pipelineVideoFrame->height();
        opChainContext.inferenceImageCrops.push_back(rect);

        opChainContext.inferenceImageCropIds.push_back(frameId);
    } else {
        open_perception_kit::forEachBoxDetectionWithContentType(
            *opChainContext.frameResults,
            contentType,
            [&opChainContext, pipelineVideoFrame](const auto &r) {
                if (!r.box || !r.object) {
                    return;
                }

                const auto projected = open_perception_kit::projectBoundingBox(
                    *r.box, pipelineVideoFrame->width(), pipelineVideoFrame->height());
                if (!projected)
                    return;
                const auto left = std::clamp(
                    (*projected)[0], 0.0, static_cast<double>(pipelineVideoFrame->width()));
                const auto top = std::clamp(
                    (*projected)[1], 0.0, static_cast<double>(pipelineVideoFrame->height()));
                const auto right = std::clamp((*projected)[0] + (*projected)[2],
                                              left,
                                              static_cast<double>(pipelineVideoFrame->width()));
                const auto bottom = std::clamp((*projected)[1] + (*projected)[3],
                                               top,
                                               static_cast<double>(pipelineVideoFrame->height()));
                // Preserve existing truncation for valid, unclipped pixel crops.
                const opk::PixelRect rect{static_cast<size_t>(left),
                                          static_cast<size_t>(top),
                                          static_cast<size_t>(right - left),
                                          static_cast<size_t>(bottom - top)};
                if (rect.isEmpty())
                    return;

                opChainContext.inferenceImageCrops.push_back(rect);
                opChainContext.inferenceImageCropIds.push_back(r.object->id);
            });
    }

    return opk::op::OpSignal::Continue;
}
