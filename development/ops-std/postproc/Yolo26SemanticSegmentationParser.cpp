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

#include "postproc/Yolo26SemanticSegmentationParser.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace opk::stdop::postproc {
namespace {

constexpr std::size_t ModelSize = 1024U;
constexpr std::uint8_t ClassCount = 19U;
// FlatBuffers uses signed 32-bit offsets; reserve space for tables and layer metadata.
constexpr std::size_t MaxMaskBytes = std::numeric_limits<std::int32_t>::max() - 65536U;

opk::Result<void> validateGeometry(const opk::TensorParser::Input &input,
                                   const open_perception_kit::FrameResults &results) {
    const auto &image = input.inferenceInfo.image;
    if (image.modelWidth != ModelSize || image.modelHeight != ModelSize) {
        return tl::unexpected(OPK_ERROR(
            opk::ErrorFlag::InvalidData,
            "Yolo26SemanticSegmentationParser: model image dimensions must be 1024x1024"));
    }
    if (image.width == 0U || image.height == 0U ||
        image.width > std::numeric_limits<std::uint32_t>::max() ||
        image.height > std::numeric_limits<std::uint32_t>::max() ||
        image.width > MaxMaskBytes / image.height) {
        return tl::unexpected(OPK_ERROR(
            opk::ErrorFlag::InvalidData,
            "Yolo26SemanticSegmentationParser: source dimensions exceed bitmap or packet limits"));
    }
    if (image.letterboxLeft >= ModelSize || image.letterboxRight >= ModelSize ||
        image.letterboxTop >= ModelSize || image.letterboxBottom >= ModelSize ||
        image.letterboxLeft + image.letterboxRight >= ModelSize ||
        image.letterboxTop + image.letterboxBottom >= ModelSize) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "Yolo26SemanticSegmentationParser: invalid letterbox padding"));
    }

    bool fullFrame = false;
    results.for_each<open_perception_kit::metadata::FrameContextT>([&](const auto &context) {
        if (!context.video || !context.video->object ||
            context.video->object->id != input.inferenceInfo.parentId) {
            return;
        }
        const auto &video = *context.video;
        fullFrame = video.original_width == image.width && video.original_height == image.height &&
                    video.source_crop_left == 0U && video.source_crop_right == 0U &&
                    video.source_crop_top == 0U && video.source_crop_bottom == 0U;
    });
    if (!fullFrame || input.inferenceInfo.parentId == 0U) {
        return tl::unexpected(OPK_ERROR(
            opk::ErrorFlag::InvalidData,
            "Yolo26SemanticSegmentationParser: requires an uncropped full-frame FrameContext "
            "parent; ROI inference is unsupported"));
    }
    return {};
}

std::vector<std::size_t> makeNearestSamples(std::size_t inputSize, std::size_t outputSize) {
    std::vector<std::size_t> samples(outputSize);
    const float scale = static_cast<float>(inputSize) / static_cast<float>(outputSize);
    for (std::size_t index = 0U; index < outputSize; ++index) {
        // Match PyTorch mode="nearest": floor the scaled index, without a half-pixel shift.
        samples[index] =
            std::min(static_cast<std::size_t>(static_cast<float>(index) * scale), inputSize - 1U);
    }
    return samples;
}

} // namespace

opk::Result<void>
Yolo26SemanticSegmentationParser::parse(const Input &input,
                                        open_perception_kit::FrameResults &results) {
    if (!input.tensors[0]) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "Yolo26SemanticSegmentationParser: class-map tensor is required"));
    }
    const auto &output = *input.tensors[0];
    if (output.getValueType() != opk::Dtype::Uint8 ||
        !(output.getShape() == opk::Shape(1, 1024, 1024))) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "Yolo26SemanticSegmentationParser: expected Uint8 tensor [1,1024,1024]"));
    }
    if (!output.isValid()) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "Yolo26SemanticSegmentationParser: invalid tensor backing"));
    }
    if (const auto geometry = validateGeometry(input, results); !geometry) {
        return tl::unexpected(geometry.error());
    }

    std::vector<std::uint8_t> classes(output.getCount());
    for (std::size_t index = 0U; index < classes.size(); ++index) {
        const float value = output.get(index);
        if (!std::isfinite(value) || value < 0.0F || value >= ClassCount ||
            std::floor(value) != value) {
            return tl::unexpected(OPK_ERROR(
                opk::ErrorFlag::InvalidData,
                "Yolo26SemanticSegmentationParser: class IDs must be finite integers in [0,18]"));
        }
        classes[index] = static_cast<std::uint8_t>(value);
    }

    const auto &image = input.inferenceInfo.image;
    const auto activeWidth = ModelSize - image.letterboxLeft - image.letterboxRight;
    const auto activeHeight = ModelSize - image.letterboxTop - image.letterboxBottom;
    const auto xSamples = makeNearestSamples(activeWidth, image.width);
    const auto ySamples = makeNearestSamples(activeHeight, image.height);
    opk::Bitmap bitmap(opk::Bitmap::Type::Uint8, image.width, image.height);
    for (std::size_t y = 0U; y < image.height; ++y) {
        const auto row = (image.letterboxTop + ySamples[y]) * ModelSize + image.letterboxLeft;
        for (std::size_t x = 0U; x < image.width; ++x) {
            bitmap.set8(x, y, classes[row + xSamples[x]]);
        }
    }

    auto mask = std::make_unique<open_perception_kit::metadata::SemanticSegmentationMaskT>();
    mask->object = open_perception_kit::makeObjectMeta(0U, input.inferenceInfo.parentId);
    mask->bitmap = open_perception_kit::makeBitmapData(bitmap);

    open_perception_kit::metadata::SemanticSegmentationMasksT payload;
    payload.layer =
        open_perception_kit::makeLayerInfo({.model = input.inferenceInfo.modelName,
                                            .inferElementId = input.inferenceInfo.inferElementId,
                                            .contentType = k_content_type,
                                            .labelFamily = "cityscapes",
                                            .producer = &input.producerInfo});
    payload.masks.push_back(std::move(mask));
    results.add(std::move(payload));
    return {};
}

} // namespace opk::stdop::postproc
