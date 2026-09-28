/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "postproc/PersonClassificationParser.h"

#include <algorithm>
#include <cmath>
#include <fmt/core.h>
#include <memory>
#include <utility>

using namespace opk;
using namespace opk::stdop::postproc;

opk::Result<void> PersonClassificationParser::parse(const opk::TensorParser::Input &input,
                                                    open_perception_kit::FrameResults &results) {

    if (!input.tensors[0]) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData, "PersonClassificationParser: input tensor is null"));
    }

    const auto shape = input.tensors[0]->getShape();
    if (shape.rank != 2) {
        return tl::unexpected(OPK_ERROR(
            ErrorFlag::InvalidData,
            fmt::format("PersonClassificationParser: expected 2D tensor, got {}D", shape.rank)));
    }

    if (shape.dims[0] != 1) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData,
                      fmt::format("PersonClassificationParser: batch size must be 1, got {}",
                                  shape.dims[0])));
    }
    if (shape.dims[1] != 2) {
        return tl::unexpected(OPK_ERROR(
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

    auto result = std::make_unique<open_perception_kit::metadata::PersonPresenceT>();
    result->object = open_perception_kit::makeObjectMeta(0U, input.inferenceInfo.parentId);
    result->yes_confidence = yesConfidence;
    result->no_confidence = noConfidence;

    open_perception_kit::metadata::ClassificationsT payload;
    payload.layer =
        open_perception_kit::makeLayerInfo({.model = input.inferenceInfo.modelName,
                                            .inferElementId = input.inferenceInfo.inferElementId,
                                            .contentType = k_content_type,
                                            .producer = &input.producerInfo});
    payload.person_presence.push_back(std::move(result));
    results.add(std::move(payload));

    return {};
}
