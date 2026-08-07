/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "InferenceControllerOp.h"

#include <fmt/core.h>

#include "pek/Perception.h"
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
    opChainContext.rootLayer.inferElementId =
        "rootLayer_" + opChainContext.inferenceInfo.inferElementId;
    opChainContext.rootLayer.contentType = "videoFrame";

    if (contentType.empty()) {
        // setup source VideoFrame object
        pek::Perception::VideoFrame videoFrame;
        videoFrame.originalWidth = pipelineVideoFrame->width();
        videoFrame.originalHeight = pipelineVideoFrame->height();
        opChainContext.inferenceSourceUuid = videoFrame.uuid;
        opChainContext.rootLayer.detections.emplace_back(videoFrame);

        pek::PixelRect rect;
        rect.x = 0;
        rect.y = 0;
        rect.width = pipelineVideoFrame->width();
        rect.height = pipelineVideoFrame->height();
        opChainContext.inferenceImageCrops.push_back(rect);

        opChainContext.inferenceImageCropUuids.push_back(videoFrame.uuid);
    } else {
        pek::PerceptionTools perception(*opChainContext.perception);
        auto rects = perception.getAllRectsWithContentType(contentType);

        for (const auto &r : rects) {
            pek::PixelRect rect;
            rect.x = (int)r.x;
            rect.y = (int)r.y;
            rect.width = (int)r.width;
            rect.height = (int)r.height;

            opChainContext.inferenceImageCrops.push_back(rect);
            opChainContext.inferenceImageCropUuids.push_back(r.uuid);
        }
    }

    return pek::op::OpSignal::Continue;
}
