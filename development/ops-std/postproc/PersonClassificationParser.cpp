/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "postproc/PersonClassificationParser.h"
#include "pek/Perception.h"

#include <algorithm>
#include <cmath>
#include <fmt/core.h>

using namespace pek;
using namespace pek::stdop::postproc;

Result<void> PersonClassificationParser::parse(const pek::TensorParser::Input &input,
                                               pek::Perception::Layer &detectionResult) {

    if (!input.tensors[0]) {
        return tl::unexpected(
            PEK_ERROR(ErrorFlag::InvalidData, "PersonClassificationParser: input tensor is null"));
    }

    const auto shape = input.tensors[0]->getShape();
    if (shape.rank != 2) {
        return tl::unexpected(PEK_ERROR(
            ErrorFlag::InvalidData,
            fmt::format("PersonClassificationParser: expected 2D tensor, got {}D", shape.rank)));
    }

    if (shape.dims[0] != 1) {
        return tl::unexpected(
            PEK_ERROR(ErrorFlag::InvalidData,
                      fmt::format("PersonClassificationParser: batch size must be 1, got {}",
                                  shape.dims[0])));
    }
    if (shape.dims[1] != 2) {
        return tl::unexpected(PEK_ERROR(
            ErrorFlag::InvalidData,
            fmt::format("PersonClassificationParser: expected 2 classes, got {}", shape.dims[1])));
    }

    const float rawYes = input.tensors[0]->get(0);
    const float rawNo = input.tensors[0]->get(1);

    float noConfidence = rawNo;
    float yesConfidence = rawYes;

    // Some models output logits while others output probabilities.
    // If output does not look like probabilities, apply softmax.
    const float sum = rawNo + rawYes;
    const bool looksLikeProbabilities =
        rawNo >= 0.0f && rawYes >= 0.0f && std::abs(sum - 1.0f) < 1e-3f;

    if (!looksLikeProbabilities) {
        const float maxLogit = std::max(rawNo, rawYes);
        const float expNo = std::exp(rawNo - maxLogit);
        const float expYes = std::exp(rawYes - maxLogit);
        const float denom = expNo + expYes;
        if (denom > 0.0f) {
            noConfidence = expNo / denom;
            yesConfidence = expYes / denom;
        }
    }

    detectionResult.contentType = "personClassification";
    Perception::PersonClassification result;
    result.yesConfidence = yesConfidence;
    result.noConfidence = noConfidence;
    detectionResult.detections.push_back(result);

    return {};
}
