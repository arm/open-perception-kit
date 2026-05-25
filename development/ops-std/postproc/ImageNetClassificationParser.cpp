/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "postproc/ImageNetClassificationParser.h"
#include "pek/Labels.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fmt/core.h>
#include <span>
#include <vector>

using namespace pek;
using namespace pek::stdop::postproc;

// Softmax helper function
static void softmax(const std::span<float> input, std::span<float> output) {

    if (input.size() != output.size()) {
        std::fill(output.begin(), output.end(), 0.0f);
        return;
    }

    if (input.empty()) {
        return;
    }

    auto it = std::max_element(input.begin(), input.end());
    float max = *it;
    auto sum = 0.0f;
    for (auto i = 0U; i < output.size(); ++i) {
        output[i] = std::exp(input[i] - max);
        sum += output[i];
    }
    for (auto i = 0U; i < output.size(); ++i) {
        output[i] /= sum;
    }
}

Result<void> ImageNetClassificationParser::parse(const pek::TensorParser::Input &input,
                                                 pek::Perception::Layer &detectionResult) {

    if (!input.tensors[0]) {
        return tl::unexpected(PEK_ERROR(pek::ErrorFlag::InvalidData,
                                        "ImageNetClassificationParser: input tensor is null"));
    }

    const auto shape = input.tensors[0]->getShape();
    if (shape.rank != 2U) {
        return tl::unexpected(PEK_ERROR(
            pek::ErrorFlag::InvalidData,
            fmt::format("ImageNetClassificationParser: expected 2D tensor, got {}D", shape.rank)));
    }
    if (shape.dims[0] != 1U) {
        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("ImageNetClassificationParser: batch size must be 1, got {}",
                                  shape.dims[0])));
    }

    constexpr auto numClasses = Labels::getLabelCount(LabelType::ImageNet);
    if (numClasses != shape.dims[1]) {
        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("ImageNetClassificationParser: expected {} classes, got {}",
                                  numClasses,
                                  shape.dims[1])));
    }

    const int topK = input.attributes.getIntOrDefault("topK", 5);
    const float confidenceThreshold =
        input.attributes.getDoubleOrDefault("confidenceThreshold", 0.01);

    std::array<float, numClasses> probabilities;
    std::array<float, numClasses> logits;

    for (size_t i = 0U; i < numClasses; ++i) {
        logits[i] = input.tensors[0]->get(i);
    }

    softmax(logits, probabilities);

    std::vector<std::pair<float, int>> scoredIndices;
    scoredIndices.reserve(numClasses);

    for (size_t i = 0; i < numClasses; ++i) {
        if (probabilities[i] >= confidenceThreshold) {
            scoredIndices.push_back({probabilities[i], static_cast<int>(i)});
        }
    }

    const auto numResults = std::min(topK, static_cast<int>(scoredIndices.size()));

    if (0 < numResults) {
        // Partial sort with std::greater<> - only sorts top-K
        std::partial_sort(scoredIndices.begin(),
                          scoredIndices.begin() + numResults,
                          scoredIndices.end(),
                          std::greater<>());

        pek::Perception::Classification classification;

        detectionResult.contentType = "classification";

        classification.candidates.reserve(numResults);
        for (int i = 0; i < numResults; ++i) {
            const auto &[confidence, classIdx] = scoredIndices[i];

            // Store classification result as DetectionRect
            // x,y will be used to position the label in lower-right corner
            // w,h are not used for classification (no actual bounding box)
            pek::Perception::Classification::Candidate candidate;
            candidate.x = 0.0f;                  // Position will be calculated by renderer
            candidate.y = static_cast<float>(i); // Store index for rendering
            candidate.w = 0.0f;                  // Not used
            candidate.h = 0.0f;                  // Not used
            candidate.confidence = confidence;
            candidate.text = theImageNetLabels[classIdx];

            classification.candidates.push_back(candidate);
        }

        detectionResult.detections.push_back(classification);
    }

    return {};
}
