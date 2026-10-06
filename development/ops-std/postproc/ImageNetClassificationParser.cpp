/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "postproc/ImageNetClassificationParser.h"
#include "opk/Labels.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fmt/core.h>
#include <memory>
#include <span>
#include <utility>
#include <vector>

using namespace opk;
using namespace opk::stdop::postproc;

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

opk::Result<void> ImageNetClassificationParser::parse(const opk::TensorParser::Input &input,
                                                      open_perception_kit::FrameResults &results) {

    if (!input.tensors[0]) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                        "ImageNetClassificationParser: input tensor is null"));
    }

    const auto shape = input.tensors[0]->getShape();
    if (shape.rank != 2U) {
        return tl::unexpected(OPK_ERROR(
            opk::ErrorFlag::InvalidData,
            fmt::format("ImageNetClassificationParser: expected 2D tensor, got {}D", shape.rank)));
    }
    if (shape.dims[0] != 1U) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      fmt::format("ImageNetClassificationParser: batch size must be 1, got {}",
                                  shape.dims[0])));
    }

    constexpr auto numClasses = resources::Labels::getLabelCount(resources::LabelType::ImageNet);
    if (numClasses != shape.dims[1]) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
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

        open_perception_kit::metadata::ClassificationsT payload;
        payload.layer = open_perception_kit::makeLayerInfo(
            {.model = input.inferenceInfo.modelName,
             .inferElementId = input.inferenceInfo.inferElementId,
             .contentType = k_content_type,
             .producer = &input.producerInfo});

        auto classification = std::make_unique<open_perception_kit::metadata::ClassificationT>();
        classification->object =
            open_perception_kit::makeObjectMeta(0U, input.inferenceInfo.parentId);

        classification->candidates.reserve(numResults);
        for (int i = 0; i < numResults; ++i) {
            const auto &[confidence, classIdx] = scoredIndices[i];

            // Store classification result as DetectionRect
            // x,y will be used to position the label in lower-right corner
            // w,h are not used for classification (no actual bounding box)
            auto candidate =
                std::make_unique<open_perception_kit::metadata::ClassificationCandidateT>();
            candidate->x = 0.0f;                  // Position will be calculated by renderer
            candidate->y = static_cast<float>(i); // Store index for rendering
            candidate->w = 0.0f;                  // Not used
            candidate->h = 0.0f;                  // Not used
            candidate->confidence = confidence;
            candidate->class_id = classIdx;
            candidate->text =
                opk::resources::Labels::getLabel(opk::resources::LabelType::ImageNet, classIdx);

            classification->candidates.push_back(std::move(candidate));
        }

        payload.classifications.push_back(std::move(classification));
        results.add(std::move(payload));
    }

    return {};
}
