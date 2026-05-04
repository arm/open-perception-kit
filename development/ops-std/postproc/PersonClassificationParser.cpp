/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "postproc/PersonClassificationParser.h"
#include "amp/Perception.h"

#include <algorithm>
#include <cmath>

using namespace amp;

amp::Result<void> PersonClassificationParser::parse(const amp::TensorParser::Input &input,
                                                    amp::Perception::Layer &detectionResult) {

    assert(input.tensors[0]);

    assert(input.tensors[0]->getShape().dimensionCount == 2);

    assert(input.tensors[0]->getShape().valueCount[0] == 1);
    assert(input.tensors[0]->getShape().valueCount[1] == 2);

    const float rawNo = input.tensors[0]->get(0);
    const float rawYes = input.tensors[0]->get(1);

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
