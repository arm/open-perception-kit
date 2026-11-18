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

#include "postproc/MobileSamMaskParser.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace opk::stdop::postproc {
namespace {

// Fixed dimensions in Arm/mobile-sam-int8-xnnpack-executorch's author config.
constexpr size_t ModelSide = 1024;
constexpr size_t LogitSide = 256;
constexpr size_t CandidateCount = 3;
constexpr size_t CandidatePixels = LogitSide * LogitSide;
// Leave space for FlatBuffers tables and the other FrameResults payloads.
constexpr size_t MaxMaskBytes = std::numeric_limits<int32_t>::max() - 65536U;

struct AxisSample {
    size_t first;
    size_t second;
    float weight;
};

constexpr std::array<AxisSample, ModelSide> makeSamples() {
    std::array<AxisSample, ModelSide> samples{};
    for (size_t index = 0; index < ModelSide; ++index) {
        const float coordinate =
            std::clamp((static_cast<float>(index) + 0.5F) * static_cast<float>(LogitSide) /
                               static_cast<float>(ModelSide) -
                           0.5F,
                       0.0F,
                       static_cast<float>(LogitSide - 1));
        const auto first = static_cast<size_t>(coordinate);
        samples[index] = {first, std::min(first + 1, LogitSide - 1), coordinate - first};
    }
    return samples;
}

constexpr auto Samples = makeSamples();

opk::Result<void> validateGeometry(const opk::TensorParser::Input &input,
                                   const open_perception_kit::FrameResults &results) {
    const auto &image = input.inferenceInfo.image;
    if (image.modelWidth != ModelSide || image.modelHeight != ModelSide || image.width == 0 ||
        image.height == 0 || image.width > MaxMaskBytes / image.height ||
        image.width > std::numeric_limits<uint32_t>::max() ||
        image.height > std::numeric_limits<uint32_t>::max() || image.letterboxLeft != 0 ||
        image.letterboxRight != 0 || image.letterboxTop != 0 || image.letterboxBottom != 0) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "MobileSamMaskParser requires a valid 1024-square full-image stretch"));
    }
    bool fullFrame = false;
    results.for_each<open_perception_kit::metadata::FrameContextT>([&](const auto &context) {
        if (context.video && context.video->object &&
            context.video->object->id == input.inferenceInfo.parentId) {
            const auto &video = *context.video;
            fullFrame = video.original_width == image.width &&
                        video.original_height == image.height && video.source_crop_left == 0 &&
                        video.source_crop_top == 0 && video.source_crop_right == 0 &&
                        video.source_crop_bottom == 0;
        }
    });
    if (!fullFrame || input.inferenceInfo.parentId == 0) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "MobileSamMaskParser requires an uncropped full-frame parent"));
    }
    return {};
}

std::vector<uint8_t> upsampleAndThreshold(const std::vector<float> &logits) {
    std::vector<uint8_t> binary(ModelSide * ModelSide);
    for (size_t y = 0; y < ModelSide; ++y) {
        const auto &sy = Samples[y];
        for (size_t x = 0; x < ModelSide; ++x) {
            const auto &sx = Samples[x];
            const float upper = (1.0F - sx.weight) * logits[sy.first * LogitSide + sx.first] +
                                sx.weight * logits[sy.first * LogitSide + sx.second];
            const float lower = (1.0F - sx.weight) * logits[sy.second * LogitSide + sx.first] +
                                sx.weight * logits[sy.second * LogitSide + sx.second];
            const float value = (1.0F - sy.weight) * upper + sy.weight * lower;
            binary[y * ModelSide + x] = value > 0.0F ? 1 : 0;
        }
    }
    return binary;
}

} // namespace

std::vector<std::string_view> MobileSamMaskParser::getProvidedContentTypes() const {
    return {k_content_type};
}

opk::Result<void> MobileSamMaskParser::parse(const Input &input,
                                             open_perception_kit::FrameResults &results) {
    if (!input.tensors[0] || !input.tensors[1] || !input.tensors[0]->isValid() ||
        !input.tensors[1]->isValid() || input.tensors[0]->getValueType() != opk::Dtype::Float32 ||
        input.tensors[1]->getValueType() != opk::Dtype::Float32 ||
        !(input.tensors[0]->getShape() == opk::Shape(1, 3, 256, 256)) ||
        !(input.tensors[1]->getShape() == opk::Shape(1, 3))) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "MobileSamMaskParser expects Float32 [1,3,256,256] logits and [1,3] scores"));
    }
    const auto geometry = validateGeometry(input, results);
    if (!geometry) {
        return tl::unexpected(geometry.error());
    }
    size_t bestIndex = 0;
    float bestScore = -std::numeric_limits<float>::infinity();
    for (size_t index = 0; index < CandidateCount; ++index) {
        const float score = input.tensors[1]->get(index);
        if (!std::isfinite(score)) {
            return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                            "MobileSamMaskParser IoU scores must be finite"));
        }
        // Strict greater-than retains the first maximum, matching torch.argmax.
        if (score > bestScore) {
            bestIndex = index;
            bestScore = score;
        }
    }
    std::vector<float> logits(CandidatePixels);
    for (size_t index = 0; index < CandidatePixels; ++index) {
        logits[index] = input.tensors[0]->get(bestIndex * CandidatePixels + index);
        if (!std::isfinite(logits[index])) {
            return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                            "MobileSamMaskParser selected logits must be finite"));
        }
    }
    // Threshold in model space BEFORE nearest restoration; direct logit resizing differs.
    const auto binary = upsampleAndThreshold(logits);
    const auto &image = input.inferenceInfo.image;
    opk::Bitmap bitmap(opk::Bitmap::Type::Uint8, image.width, image.height);
    size_t left = image.width;
    size_t top = image.height;
    size_t right = 0;
    size_t bottom = 0;
    for (size_t y = 0; y < image.height; ++y) {
        const size_t sourceY = y * ModelSide / image.height;
        for (size_t x = 0; x < image.width; ++x) {
            const size_t sourceX = x * ModelSide / image.width;
            const auto value = binary[sourceY * ModelSide + sourceX];
            bitmap.set8(x, y, value);
            if (value != 0) {
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x + 1);
                bottom = std::max(bottom, y + 1);
            }
        }
    }
    auto mask = std::make_unique<open_perception_kit::metadata::InstanceSegmentationMaskT>();
    mask->object = open_perception_kit::makeObjectMeta(0, input.inferenceInfo.parentId);
    mask->bitmap = open_perception_kit::makeBitmapData(bitmap);
    mask->confidence = bestScore;
    if (right > left && bottom > top) {
        mask->box = open_perception_kit::makeBoundingBox(static_cast<float>(left),
                                                         static_cast<float>(top),
                                                         static_cast<float>(right - left),
                                                         static_cast<float>(bottom - top));
    }
    open_perception_kit::metadata::InstanceSegmentationMasksT payload;
    payload.layer =
        open_perception_kit::makeLayerInfo({.model = input.inferenceInfo.modelName,
                                            .inferElementId = input.inferenceInfo.inferElementId,
                                            .contentType = k_content_type,
                                            .producer = &input.producerInfo});
    payload.masks.push_back(std::move(mask));
    if (input.prompt) {
        opk::appendPromptAssociation(
            results, *input.prompt, *payload.layer, input.inferenceInfo.parentId);
    }
    results.add(std::move(payload));
    return {};
}

} // namespace opk::stdop::postproc
