/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "postproc/CameraContactParser.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <fmt/core.h>

using namespace amp;

namespace {

std::array<float, 2> softmax2(const amp::TensorView &tensor) {
    const float maxLogit = std::max(tensor.get(0), tensor.get(1));
    const float exp0 = std::exp(tensor.get(0) - maxLogit);
    const float exp1 = std::exp(tensor.get(1) - maxLogit);
    const float sum = exp0 + exp1;

    if (!(sum > 0.0f) || !std::isfinite(sum)) {
        return {0.5f, 0.5f};
    }

    return {exp0 / sum, exp1 / sum};
}

} // namespace

amp::Result<void> CameraContactParser::parse(const amp::TensorParser::Input &input,
                                             amp::Perception::Layer &detectionResult) {
    assert(input.tensors[0]);

    const auto &tensor = *input.tensors[0];
    const auto shape = tensor.getShape();

    if (shape.dimensionCount != 2 || shape.valueCount[0] != 1 || shape.valueCount[1] != 2) {
        return tl::unexpected(AMP_ERROR(
            amp::ErrorFlag::InvalidData,
            fmt::format("CameraContactParser expects [1,2] logits, got {}", shape.toString())));
    }

    const int contactClassIndex =
        static_cast<int>(input.attributes.getIntOrDefault("contactClassIndex", 1));
    const int noContactClassIndex =
        static_cast<int>(input.attributes.getIntOrDefault("noContactClassIndex", 0));

    if (contactClassIndex == noContactClassIndex || contactClassIndex < 0 ||
        contactClassIndex > 1 || noContactClassIndex < 0 || noContactClassIndex > 1) {
        return tl::unexpected(AMP_ERROR(
            amp::ErrorFlag::InvalidData,
            fmt::format(
                "CameraContactParser requires distinct class indices in [0,1], got {} and {}",
                contactClassIndex,
                noContactClassIndex)));
    }

    const auto probabilities = softmax2(tensor);
    const bool isContact = probabilities[static_cast<size_t>(contactClassIndex)] >=
                           probabilities[static_cast<size_t>(noContactClassIndex)];

    amp::Perception::Classification classification;
    amp::Perception::Classification::Candidate candidate;
    candidate.classId = isContact ? contactClassIndex : noContactClassIndex;
    candidate.confidence = isContact ? probabilities[static_cast<size_t>(contactClassIndex)]
                                     : probabilities[static_cast<size_t>(noContactClassIndex)];
    candidate.text = isContact ? "contact" : "no contact";

    classification.candidates.push_back(candidate);

    detectionResult.contentType = "cameraContact";
    detectionResult.detections.push_back(classification);

    return {};
}