/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "postproc/GazeDetectionParser.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>

using namespace opk;
using namespace opk::stdop::postproc;

namespace {

// Soft-argmax over bins -> angle in degrees.
// Also returns a confidence in [0,1] as max softmax probability.
inline void logitsToAngleDegAndConfidence(const opk::TensorView *logits,
                                          float &outAngleDeg,
                                          float &outConfidence,
                                          float angleBinWidthDeg = 0.0f) {
    const size_t n = logits ? logits->getCount() : 0;
    if (!logits || n == 0) {
        outAngleDeg = 0.0f;
        outConfidence = 0.0f;
        return;
    }

    // Find max for stable softmax.
    float maxLogit = logits->get(0);
    for (size_t i = 1; i < n; ++i)
        maxLogit = std::max(maxLogit, logits->get(i));

    // Compute exp(logit-max) once.
    float sum = 0.0f;
    float expected = 0.0f;
    float maxProb = 0.0f;

    for (size_t i = 0; i < n; ++i) {
        const float e = std::exp(logits->get(i) - maxLogit);
        sum += e;
    }

    // Avoid div-by-zero if all logits are -inf or sum underflows.
    if (!(sum > 0.0f) || !std::isfinite(sum)) {
        outAngleDeg = 0.0f;
        outConfidence = 0.0f;
        return;
    }

    for (size_t i = 0; i < n; ++i) {
        const float p = std::exp(logits->get(i) - maxLogit) / sum;
        expected += static_cast<float>(i) * p;
        maxProb = std::max(maxProb, p);
    }

    if (angleBinWidthDeg > 0.0f) {
        outAngleDeg = (expected - static_cast<float>(n) / 2.0f) * angleBinWidthDeg;
    } else {
        // Preserve the original mapping for OpChains that do not configure bin width.
        constexpr static float gazeRangeDeg = 90.0f;
        const auto denom = static_cast<float>(n - 1);
        const float step = (denom > 0.0f) ? ((2.0f * gazeRangeDeg) / denom) : 0.0f;
        outAngleDeg = expected * step - gazeRangeDeg;
    }
    outConfidence = std::clamp(maxProb, 0.0f, 1.0f);
}

} // namespace

opk::Result<void> GazeDetectionParser::parse(const opk::TensorParser::Input &input,
                                             open_perception_kit::FrameResults &results) {
    // Validate tensor pointers.
    if (!input.tensors[0] || !input.tensors[1]) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData, "GazeDetectionParser: input tensors are null"));
    }

    // Validate tensor shapes.
    if (input.tensors[0]->getShape().rank != 2) {
        return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData,
                                        "GazeDetectionParser: yaw tensor must have 2 dimensions"));
    }
    if (input.tensors[1]->getShape().rank != 2) {
        return tl::unexpected(OPK_ERROR(
            ErrorFlag::InvalidData, "GazeDetectionParser: pitch tensor must have 2 dimensions"));
    }

    // Validate tensor dimensions: [1, 90].
    if (input.tensors[0]->getShape().dims[0] != 1 || input.tensors[0]->getShape().dims[1] != 90) {
        return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData,
                                        "GazeDetectionParser: yaw tensor shape must be [1, 90]"));
    }
    if (input.tensors[1]->getShape().dims[0] != 1 || input.tensors[1]->getShape().dims[1] != 90) {
        return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData,
                                        "GazeDetectionParser: pitch tensor shape must be [1, 90]"));
    }

    float yaw = 0.0f, yawConf = 0.0f;
    float pitch = 0.0f, pitchConf = 0.0f;
    const auto angleBinWidthDeg =
        static_cast<float>(input.attributes.getDoubleOrDefault("angleBinWidthDeg", 0.0));

    logitsToAngleDegAndConfidence(input.tensors[0], yaw, yawConf, angleBinWidthDeg);
    logitsToAngleDegAndConfidence(input.tensors[1], pitch, pitchConf, angleBinWidthDeg);

    auto result = std::make_unique<open_perception_kit::metadata::PoseEstimationT>();
    result->object = open_perception_kit::makeObjectMeta(0U, input.inferenceInfo.parentId);
    result->yaw = yaw;
    result->pitch = pitch;

    // Store a single confidence for the pair.
    // Common choices: min (conservative), average, or max.
    // Using min makes it "both yaw and pitch must be confident".
    result->confidence = std::min(yawConf, pitchConf);

    open_perception_kit::metadata::PoseEstimationsT payload;
    payload.layer =
        open_perception_kit::makeLayerInfo({.model = input.inferenceInfo.modelName,
                                            .inferElementId = input.inferenceInfo.inferElementId,
                                            .contentType = k_content_type,
                                            .producer = &input.producerInfo});
    payload.poses.push_back(std::move(result));
    results.add(std::move(payload));
    return {};
}
