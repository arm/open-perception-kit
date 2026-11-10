/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

#include "postproc/RetinaFaceParser.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace opk::stdop::postproc {
namespace {

struct Candidate {
    size_t index;
    float score, left, top, right, bottom;
};

float intersectionOverUnion(const Candidate &a, const Candidate &b) {
    // Match the upstream RetinaFace inclusive-pixel NMS convention.
    const float width =
        std::max(0.0f, std::min(a.right, b.right) - std::max(a.left, b.left) + 1.0f);
    const float height =
        std::max(0.0f, std::min(a.bottom, b.bottom) - std::max(a.top, b.top) + 1.0f);
    const float intersection = width * height;
    const float areaA = (a.right - a.left + 1.0f) * (a.bottom - a.top + 1.0f);
    const float areaB = (b.right - b.left + 1.0f) * (b.bottom - b.top + 1.0f);
    return intersection / (areaA + areaB - intersection);
}

} // namespace

opk::Result<void> RetinaFaceParser::parse(const opk::TensorParser::Input &input,
                                          open_perception_kit::FrameResults &results) {
    const auto &image = input.inferenceInfo.image;
    const float threshold =
        static_cast<float>(input.attributes.getDoubleOrDefault("confidenceThreshold", 0.6));
    const float iouThreshold =
        static_cast<float>(input.attributes.getDoubleOrDefault("iouThreshold", 0.4));
    if (image.modelWidth == 0 || image.modelHeight == 0 || image.width == 0 || image.height == 0 ||
        image.modelWidth > std::numeric_limits<int>::max() ||
        image.modelHeight > std::numeric_limits<int>::max() || image.letterboxLeft ||
        image.letterboxRight || image.letterboxTop || image.letterboxBottom ||
        !std::isfinite(threshold) || threshold < 0.0f || threshold > 1.0f ||
        !std::isfinite(iouThreshold) || iouThreshold < 0.0f || iouThreshold > 1.0f) {
        return tl::unexpected(OPK_ERROR(
            opk::ErrorFlag::InvalidData,
            "RetinaFaceParser requires valid unpadded image dimensions and thresholds in [0,1]"));
    }

    constexpr std::array<size_t, 3> strides = {8, 16, 32};
    constexpr std::array<std::array<float, 2>, 3> sizes = {{{16, 32}, {64, 128}, {256, 512}}};
    size_t count = 0;
    for (const size_t stride : strides)
        count += 2 * ((image.modelWidth + stride - 1) / stride) *
                 ((image.modelHeight + stride - 1) / stride);
    constexpr std::array<int, 3> columns = {4, 2, 10};
    for (size_t i = 0; i < columns.size(); ++i) {
        const auto *const tensor = input.tensors[i];
        if (!tensor || !tensor->isValid()) {
            return tl::unexpected(
                OPK_ERROR(opk::ErrorFlag::InvalidData,
                          "RetinaFaceParser requires valid box, score and landmark tensors"));
        }
        const auto shape = tensor->getShape();
        if (shape.rank != 3 || shape.dims[0] != 1 || shape.dims[1] <= 0 ||
            static_cast<size_t>(shape.dims[1]) != count || shape.dims[2] != columns[i]) {
            return tl::unexpected(OPK_ERROR(
                opk::ErrorFlag::InvalidData,
                "RetinaFaceParser expects [1,N,4], [1,N,2], [1,N,10] matching the anchor count"));
        }
    }

    const auto &boxes = *input.tensors[0];
    const auto &scores = *input.tensors[1];
    std::vector<Candidate> candidates;
    size_t index = 0;
    for (size_t level = 0; level < strides.size(); ++level) {
        const size_t stride = strides[level];
        for (size_t y = 0; y < (image.modelHeight + stride - 1) / stride; ++y) {
            for (size_t x = 0; x < (image.modelWidth + stride - 1) / stride; ++x) {
                for (const float size : sizes[level]) {
                    const size_t current = index++;
                    const float score = scores.get(2 * current + 1);
                    if (!std::isfinite(score) || score <= threshold || score > 1.0f)
                        continue;
                    const float centerX =
                        (static_cast<float>(x) + 0.5f) * static_cast<float>(stride) +
                        boxes.get(4 * current) * 0.1f * size;
                    const float centerY =
                        (static_cast<float>(y) + 0.5f) * static_cast<float>(stride) +
                        boxes.get(4 * current + 1) * 0.1f * size;
                    const float width = size * std::exp(boxes.get(4 * current + 2) * 0.2f);
                    const float height = size * std::exp(boxes.get(4 * current + 3) * 0.2f);
                    const Candidate candidate{current,
                                              score,
                                              centerX - width / 2,
                                              centerY - height / 2,
                                              centerX + width / 2,
                                              centerY + height / 2};
                    if (width > 0 && height > 0 && std::isfinite(candidate.left) &&
                        std::isfinite(candidate.top) && std::isfinite(candidate.right) &&
                        std::isfinite(candidate.bottom))
                        candidates.push_back(candidate);
                }
            }
        }
    }
    std::ranges::sort(candidates, [](const Candidate &a, const Candidate &b) {
        return a.score == b.score ? a.index < b.index : a.score > b.score;
    });
    if (candidates.size() > 5000)
        candidates.resize(5000);
    std::vector<Candidate> kept;
    for (const auto &candidate : candidates) {
        if (std::ranges::any_of(kept, [&](const Candidate &other) {
                return intersectionOverUnion(candidate, other) > iouThreshold;
            }))
            continue;
        kept.push_back(candidate);
        if (kept.size() == 750)
            break;
    }

    open_perception_kit::metadata::BoxDetectionsT payload;
    payload.layer =
        open_perception_kit::makeLayerInfo({.model = input.inferenceInfo.modelName,
                                            .inferElementId = input.inferenceInfo.inferElementId,
                                            .contentType = "humanFace",
                                            .producer = &input.producerInfo});
    const float width = static_cast<float>(image.width);
    const float height = static_cast<float>(image.height);
    const float scaleX = width / static_cast<float>(image.modelWidth);
    const float scaleY = height / static_cast<float>(image.modelHeight);
    for (const auto &candidate : kept) {
        const float left = std::clamp(candidate.left * scaleX, 0.0f, width);
        const float top = std::clamp(candidate.top * scaleY, 0.0f, height);
        const float right = std::clamp(candidate.right * scaleX, 0.0f, width);
        const float bottom = std::clamp(candidate.bottom * scaleY, 0.0f, height);
        if (right <= left || bottom <= top)
            continue;
        auto detection = std::make_unique<open_perception_kit::metadata::BoxDetectionT>();
        detection->object = open_perception_kit::makeObjectMeta(0U, input.inferenceInfo.parentId);
        detection->box =
            open_perception_kit::makeBoundingBox(left, top, right - left, bottom - top);
        detection->confidence = candidate.score;
        detection->class_id = 0;
        payload.detections.push_back(std::move(detection));
    }
    if (!payload.detections.empty())
        results.add(std::move(payload));
    return {};
}

} // namespace opk::stdop::postproc
