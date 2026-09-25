/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "InferenceControllerOp.h"

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
        auto object = open_perception_kit::makeObjectMeta();
        const uint64_t frameId = object->id;

        open_perception_kit::metadata::FrameContextT frameContext;
        const auto producer = producerInfo(
            opChainContext.inferenceInfo.inferElementId, opName, "opk-std-ops/InferenceController");
        const auto rootLayerId = "rootLayer_" + opChainContext.inferenceInfo.inferElementId;
        frameContext.layer = open_perception_kit::makeLayerInfo(
            {.inferElementId = rootLayerId, .contentType = "frameContext", .producer = &producer});
        frameContext.video = std::make_unique<open_perception_kit::metadata::VideoFrameContextT>();
        frameContext.video->object = std::move(object);
        frameContext.video->original_width = pipelineVideoFrame->width();
        frameContext.video->original_height = pipelineVideoFrame->height();
        opChainContext.frameResults->add(std::move(frameContext));

        opk::PixelRect rect;
        rect.x = 0;
        rect.y = 0;
        rect.width = pipelineVideoFrame->width();
        rect.height = pipelineVideoFrame->height();
        opChainContext.inferenceImageCrops.push_back(rect);

        opChainContext.inferenceImageCropIds.push_back(frameId);
    } else {
        open_perception_kit::forEachBoxDetectionWithContentType(
            *opChainContext.frameResults, contentType, [&opChainContext](const auto &r) {
                if (!r.box || !r.object) {
                    return;
                }

                opk::PixelRect rect;
                rect.x = static_cast<size_t>(r.box->x);
                rect.y = static_cast<size_t>(r.box->y);
                rect.width = static_cast<size_t>(r.box->width);
                rect.height = static_cast<size_t>(r.box->height);

                opChainContext.inferenceImageCrops.push_back(rect);
                opChainContext.inferenceImageCropIds.push_back(r.object->id);
            });
    }

    return opk::op::OpSignal::Continue;
}
