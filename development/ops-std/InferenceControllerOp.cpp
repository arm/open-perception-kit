/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "InferenceControllerOp.h"

#include <fmt/core.h>
#include <memory>
#include <utility>

#include "pek/FrameResults.h"
#include "pek/Result.h"
#include "pek/Types.h"
#include "tl/expected.hpp"

#include <perf/PerformanceTracer.h>

using namespace pek::stdop;

InferenceControllerOp::InferenceControllerOp() = default;
InferenceControllerOp::~InferenceControllerOp() = default;

pek::Result<void> InferenceControllerOp::bind(size_t index, const std::vector<pek::op::Op *> &ops) {
    return {};
}

pek::Result<void> InferenceControllerOp::configure(const pek::AttributeMap &attributes) {
    contentType = attributes.getStringOrDefault("contentType", "");
    return {};
}

std::vector<std::string_view> InferenceControllerOp::getRequiredContentTypes() const {
    return contentType.empty() ? std::vector<std::string_view>{}
                               : std::vector<std::string_view>{contentType};
}

pek::Result<pek::op::OpSignal>
InferenceControllerOp::process(pek::op::OpChainContext &opChainContext) {
    auto *pipelineVideoFrame = opChainContext.getVideoFrame("pipelineVideoFrame");

    // TODO: later it can be also audio data not video only
    if (pipelineVideoFrame == nullptr) {
        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidOpChain,
                      "InferenceControllerOp needs pipelineVideoFrame VideoFrame"));
    }

    opChainContext.inferenceInfo.modelName.clear();

    if (contentType.empty()) {
        auto object = perception::makeObjectMeta();
        const uint64_t frameId = object->id;

        perception::metadata::FrameContextT frameContext;
        const auto producer = perception::makeProducerInfo(
            fmt::format("{}/{}", opChainContext.inferenceInfo.inferElementId, instanceId),
            fmt::format("{}/{}", libName, opName),
            opName);
        frameContext.layer = perception::makeLayerInfo(
            "",
            "rootLayer_" + opChainContext.inferenceInfo.inferElementId,
            "frameContext",
            "",
            "",
            "",
            "",
            producer.get());
        frameContext.video = std::make_unique<perception::metadata::VideoFrameContextT>();
        frameContext.video->object = std::move(object);
        frameContext.video->original_width = pipelineVideoFrame->width();
        frameContext.video->original_height = pipelineVideoFrame->height();
        opChainContext.frameResults->add(std::move(frameContext));

        pek::PixelRect rect;
        rect.x = 0;
        rect.y = 0;
        rect.width = pipelineVideoFrame->width();
        rect.height = pipelineVideoFrame->height();
        opChainContext.inferenceImageCrops.push_back(rect);

        opChainContext.inferenceImageCropIds.push_back(frameId);
    } else {
        perception::forEachBoxDetectionWithContentType(
            *opChainContext.frameResults, contentType, [&opChainContext](const auto &r) {
                if (!r.box || !r.object) {
                    return;
                }

                pek::PixelRect rect;
                rect.x = static_cast<size_t>(r.box->x);
                rect.y = static_cast<size_t>(r.box->y);
                rect.width = static_cast<size_t>(r.box->width);
                rect.height = static_cast<size_t>(r.box->height);

                opChainContext.inferenceImageCrops.push_back(rect);
                opChainContext.inferenceImageCropIds.push_back(r.object->id);
            });
    }

    return pek::op::OpSignal::Continue;
}
