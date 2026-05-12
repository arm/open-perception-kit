/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "InferenceControllerOp.h"

#include <fmt/core.h>

#include "pek/Perception.h"
#include "pek/Result.h"
#include "pek/Types.h"
#include "tl/expected.hpp"

#include <PerformanceTracer.h>

using namespace pek;

InferenceControllerOp::InferenceControllerOp() {}
InferenceControllerOp::~InferenceControllerOp() {}

pek::Result<void> InferenceControllerOp::bind(size_t index, const std::vector<pek::Op *> &ops) {
    return {};
}

pek::Result<void> InferenceControllerOp::configure(const pek::AttributeMap &attributes) {
    contentType = attributes.getStringOrDefault("contentType", "");
    return {};
}

pek::Result<void> InferenceControllerOp::process(pek::OpChainContext &opChainContext) {
    pek::BitmapView *pipelineVideoFrame = opChainContext.getBitmapView("pipelineVideoFrame");

    // TODO: later it can be also audio data not video only
    if (pipelineVideoFrame == nullptr) {
        return tl::unexpected(PEK_ERROR(pek::ErrorFlag::InvalidOpChain,
                                        "InferenceControllerOp needs pipelineVideoFrame"));
    }

    // we put the context into loop mode, if 0 or 1 inference is needed
    // preprocessor will break in the 1st or 2nd iteration
    opChainContext.loopId = loopId;

    opChainContext.inferenceInfo.modelFamily.clear();
    opChainContext.rootLayer.inferElementId =
        "rootLayer_" + opChainContext.inferenceInfo.inferElementId;

    if (contentType.empty()) {
        // setup source VideoFrame object
        Perception::VideoFrame videoFrame;
        videoFrame.originalWidth = pipelineVideoFrame->width;
        videoFrame.originalHeight = pipelineVideoFrame->height;
        opChainContext.inferenceSourceUuid = videoFrame.uuid;
        opChainContext.rootLayer.detections.emplace_back(videoFrame);

        pek::PixelRect rect;
        rect.x = 0;
        rect.y = 0;
        rect.width = pipelineVideoFrame->width;
        rect.height = pipelineVideoFrame->height;
        opChainContext.inferenceImageCrops.push_back(rect);

        opChainContext.inferenceImageCropUuids.push_back(videoFrame.uuid);
    } else {
        PerceptionTools perception(*opChainContext.perception);
        auto rects = perception.getAllRectsWithContentType(contentType);

        opChainContext.loopId = loopId;
        opChainContext.inferenceInfo.modelFamily = contentType;

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

    return {};
}
