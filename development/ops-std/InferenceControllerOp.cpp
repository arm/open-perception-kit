#include "InferenceControllerOp.h"

#include <fmt/core.h>

#include "amp/Perception.h"
#include "amp/Result.h"
#include "amp/TensorView.h"
#include "amp/Types.h"
#include "tl/expected.hpp"

#include <PerformanceTracer.h>

using namespace amp;

InferenceControllerOp::InferenceControllerOp() {}
InferenceControllerOp::~InferenceControllerOp() {}

amp::Result<void> InferenceControllerOp::bind(size_t index, const std::vector<amp::Op *> &ops) {
    return {};
}

amp::Result<void> InferenceControllerOp::configure(const amp::AttributeMap &attributes) {
    contentType = attributes.getStringOrDefault("contentType", "");
    return {};
}

amp::Result<void> InferenceControllerOp::process(amp::OpChainContext &opChainContext) {

    // hack to avoid multiple runs
    if (opChainContext.inferenceControllerExecuted == false) {
        opChainContext.inferenceControllerExecuted = true;
    } else {
        return {};
    }

    // ---

    amp::BitmapView *pipelineVideoFrame = opChainContext.getBitmapView("pipelineVideoFrame");

    if (pipelineVideoFrame == nullptr) {
        return tl::make_unexpected(AMP_ERROR(amp::ErrorFlag::InvalidOpChain,
                                             "InferenceControllerOp needs pipelineVideoFrame"));
    }

    if (contentType.empty()) {
        // setup source VideoFrame object
        Perception::Layer rootLayer;
        Perception::VideoFrame videoFrame;
        videoFrame.originalWidth = pipelineVideoFrame->width;
        videoFrame.originalHeight = pipelineVideoFrame->height;
        opChainContext.inferenceSourceUuid = videoFrame.uuid;
        rootLayer.detections.push_back(videoFrame);

        amp::PixelRect rect;
        rect.x = 0;
        rect.y = 0;
        rect.width = pipelineVideoFrame->width;
        rect.height = pipelineVideoFrame->height;
        opChainContext.inferenceCrops.push_back(rect);

        opChainContext.inferenceCropUuids.push_back(videoFrame.uuid);
    } else {
        PerceptionTools perception(*opChainContext.perception);
        auto rects = perception.getAllRectsWithContentType(contentType);

        for (const auto &r : rects) {
            amp::PixelRect rect;
            rect.x = (int)r.x;
            rect.y = (int)r.y;
            rect.width = (int)r.width;
            rect.height = (int)r.height;

            // fmt::print("ctrl: {} {} {} {}\n", rect.x, rect.y, rect.width, rect.height);

            opChainContext.inferenceCrops.push_back(rect);
            opChainContext.inferenceCropUuids.push_back(r.uuid);
        }
    }

    return {};
}
