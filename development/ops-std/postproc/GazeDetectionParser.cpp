/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "postproc/GazeDetectionParser.h"
#include "pek/Perception.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

using namespace pek;

namespace {

// Soft-argmax over bins -> angle in degrees.
// Also returns a confidence in [0,1] as max softmax probability.
inline void logitsToAngleDegAndConfidence(const pek::TensorView *logits,
                                          float &outAngleDeg,
                                          float &outConfidence,
                                          float gazeRangeDeg = 90.0f) {
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

    // Map expected index to [-gazeRangeDeg, +gazeRangeDeg].
    const float denom = float(n - 1);
    const float step = (denom > 0.0f) ? ((2.0f * gazeRangeDeg) / denom) : 0.0f;

    outAngleDeg = expected * step - gazeRangeDeg;
    outConfidence = std::clamp(maxProb, 0.0f, 1.0f);
}

} // namespace

pek::Result<void> GazeDetectionParser::parse(const pek::TensorParser::Input &input,
                                             pek::Perception::Layer &detectionResult) {
    // Validate tensor pointers.
    if (!input.tensors[0] || !input.tensors[1]) {
        return tl::unexpected(
            PEK_ERROR(ErrorFlag::InvalidData, "GazeDetectionParser: input tensors are null"));
    }

    // Validate tensor shapes.
    if (input.tensors[0]->getShape().rank != 2) {
        return tl::unexpected(PEK_ERROR(ErrorFlag::InvalidData,
                                        "GazeDetectionParser: yaw tensor must have 2 dimensions"));
    }
    if (input.tensors[1]->getShape().rank != 2) {
        return tl::unexpected(PEK_ERROR(
            ErrorFlag::InvalidData, "GazeDetectionParser: pitch tensor must have 2 dimensions"));
    }

    // Validate tensor dimensions: [1, 90].
    if (input.tensors[0]->getShape().dims[0] != 1 || input.tensors[0]->getShape().dims[1] != 90) {
        return tl::unexpected(PEK_ERROR(ErrorFlag::InvalidData,
                                        "GazeDetectionParser: yaw tensor shape must be [1, 90]"));
    }
    if (input.tensors[1]->getShape().dims[0] != 1 || input.tensors[1]->getShape().dims[1] != 90) {
        return tl::unexpected(PEK_ERROR(ErrorFlag::InvalidData,
                                        "GazeDetectionParser: pitch tensor shape must be [1, 90]"));
    }

    float yaw = 0.0f, yawConf = 0.0f;
    float pitch = 0.0f, pitchConf = 0.0f;

    logitsToAngleDegAndConfidence(input.tensors[0], yaw, yawConf);
    logitsToAngleDegAndConfidence(input.tensors[1], pitch, pitchConf);

    detectionResult.contentType = "eyeYawPitch";
    Perception::YawPitch result;
    result.yaw = yaw;
    result.pitch = pitch;

    // Store a single confidence for the pair.
    // Common choices: min (conservative), average, or max.
    // Using min makes it "both yaw and pitch must be confident".
    result.confidence = std::min(yawConf, pitchConf);

    detectionResult.detections.push_back(result);
    return {};
}
