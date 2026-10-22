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

#include "postproc/Yolo26InstanceSegmentationParser.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace opk::stdop::postproc {
namespace {

constexpr std::size_t ModelSize = 640U;
constexpr std::size_t CandidateCount = 300U;
constexpr std::size_t RowSize = 38U;
constexpr std::size_t PrototypeCount = 32U;
constexpr std::size_t PrototypeSize = 160U;
constexpr std::size_t PrototypePixels = PrototypeSize * PrototypeSize;
// FlatBuffers uses signed 32-bit offsets; leave room for mask tables and layer metadata.
constexpr std::size_t MaxMaskBytes = std::numeric_limits<std::int32_t>::max() - 65536U;

// Original bounding box
struct SourceBox {
    float left = 0.0F;
    float top = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;
};

// Detection row
struct Candidate {
    SourceBox box;
    float confidence = 0.0F;
    std::int32_t classId = -1;
    std::array<float, PrototypeCount> coefficients{};
};

// Describes how to interpolate one position along a prototype-grid axis
struct AxisSample {
    std::size_t first = 0U;
    std::size_t second = 0U;
    float weight = 0.0F;
};

opk::Result<void> validateGeometry(const opk::TensorParser::Input &input,
                                   const open_perception_kit::FrameResults &results) {
    const auto &image = input.inferenceInfo.image;
    if (image.modelWidth != ModelSize || image.modelHeight != ModelSize) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "Yolo26InstanceSegmentationParser: model image dimensions must be 640x640"));
    }
    if (image.width == 0U || image.height == 0U ||
        image.width > std::numeric_limits<std::uint32_t>::max() ||
        image.height > std::numeric_limits<std::uint32_t>::max() ||
        image.width > MaxMaskBytes / image.height) {
        return tl::unexpected(OPK_ERROR(
            opk::ErrorFlag::InvalidData,
            "Yolo26InstanceSegmentationParser: source dimensions exceed bitmap or packet limits"));
    }
    if (image.letterboxLeft >= ModelSize || image.letterboxRight >= ModelSize ||
        image.letterboxTop >= ModelSize || image.letterboxBottom >= ModelSize ||
        image.letterboxLeft + image.letterboxRight >= ModelSize ||
        image.letterboxTop + image.letterboxBottom >= ModelSize) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "Yolo26InstanceSegmentationParser: invalid letterbox padding"));
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
            "Yolo26InstanceSegmentationParser: requires an uncropped full-frame FrameContext "
            "parent; ROI inference is unsupported"));
    }

    return {};
}

float restoreCoordinate(float value,
                        std::size_t padding,
                        std::size_t activeSize,
                        std::size_t sourceSize) {
    const double restored = (static_cast<double>(value) - static_cast<double>(padding)) *
                            static_cast<double>(sourceSize) / static_cast<double>(activeSize);
    return static_cast<float>(std::clamp(restored, 0.0, static_cast<double>(sourceSize)));
}

std::vector<AxisSample>
makeAxisSamples(std::size_t sourceSize, std::size_t activeModelSize, std::size_t paddingBefore) {
    std::vector<AxisSample> samples(sourceSize);
    const double scale = static_cast<double>(activeModelSize) / static_cast<double>(sourceSize);
    constexpr double prototypeScale = static_cast<double>(PrototypeSize) / ModelSize;
    for (std::size_t index = 0U; index < sourceSize; ++index) {
        // Map source pixel centres through the recorded model-image geometry to the full
        // prototype grid. Keep padding fractional here so masks align with restored boxes.
        const double modelCoordinate =
            (static_cast<double>(index) + 0.5) * scale + static_cast<double>(paddingBefore);
        const double coordinate = std::clamp(
            modelCoordinate * prototypeScale - 0.5, 0.0, static_cast<double>(PrototypeSize - 1U));
        const auto first = static_cast<std::size_t>(coordinate);
        samples[index] = {first,
                          std::min(first + 1U, PrototypeSize - 1U),
                          static_cast<float>(coordinate - static_cast<double>(first))};
    }
    return samples;
}

bool fillMask(opk::Bitmap &bitmap,
              const std::vector<float> &logits,
              const std::vector<AxisSample> &xSamples,
              const std::vector<AxisSample> &ySamples,
              const SourceBox &box) {
    bool nonempty = false;
    for (std::size_t y = 0U; y < bitmap.getHeight(); ++y) {
        if (static_cast<float>(y) < box.top || static_cast<float>(y) >= box.bottom) {
            continue;
        }
        const auto &sy = ySamples[y];
        const auto firstRow = sy.first * PrototypeSize;
        const auto secondRow = sy.second * PrototypeSize;
        for (std::size_t x = 0U; x < bitmap.getWidth(); ++x) {
            if (static_cast<float>(x) < box.left || static_cast<float>(x) >= box.right) {
                continue;
            }
            const auto &sx = xSamples[x];
            const float upper = logits[firstRow + sx.first] * (1.0F - sx.weight) +
                                logits[firstRow + sx.second] * sx.weight;
            const float lower = logits[secondRow + sx.first] * (1.0F - sx.weight) +
                                logits[secondRow + sx.second] * sx.weight;
            const float value = upper * (1.0F - sy.weight) + lower * sy.weight;
            if (value > 0.0F) {
                bitmap.set8(x, y, 1U);
                nonempty = true;
            }
        }
    }
    return nonempty;
}

} // namespace

opk::Result<void>
Yolo26InstanceSegmentationParser::parse(const Input &input,
                                        open_perception_kit::FrameResults &results) {
    const double configuredThreshold =
        input.attributes.getDoubleOrDefault("confidenceThreshold", 0.25);
    if (!std::isfinite(configuredThreshold) || configuredThreshold < 0.0 ||
        configuredThreshold > 1.0) {
        return tl::unexpected(OPK_ERROR(
            opk::ErrorFlag::InvalidData,
            "Yolo26InstanceSegmentationParser: confidenceThreshold must be finite and in [0,1]"));
    }
    // Compare in the tensor's Float32 domain so a score equal to the configured threshold
    // stays filtered even when the decimal threshold is not exactly representable (e.g. 0.8).
    const float threshold = static_cast<float>(configuredThreshold);
    if (!input.tensors[0] || !input.tensors[1]) {
        return tl::unexpected(OPK_ERROR(
            opk::ErrorFlag::InvalidData,
            "Yolo26InstanceSegmentationParser: detection and prototype tensors are required"));
    }
    const auto &detections = *input.tensors[0];
    const auto &prototypes = *input.tensors[1];
    if (detections.getValueType() != opk::Dtype::Float32 ||
        prototypes.getValueType() != opk::Dtype::Float32 ||
        !(detections.getShape() == opk::Shape(1, 300, 38)) ||
        !(prototypes.getShape() == opk::Shape(1, 32, 160, 160))) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "Yolo26InstanceSegmentationParser: expected Float32 tensors [1,300,38] and "
                      "[1,32,160,160]"));
    }
    if (!detections.isValid() || !prototypes.isValid()) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "Yolo26InstanceSegmentationParser: invalid tensor backing"));
    }
    const auto geometry = validateGeometry(input, results);
    if (!geometry) {
        return tl::unexpected(geometry.error());
    }

    std::vector<float> prototypeValues(PrototypeCount * PrototypePixels);
    for (std::size_t index = 0U; index < prototypeValues.size(); ++index) {
        prototypeValues[index] = prototypes.get(index);
        if (!std::isfinite(prototypeValues[index])) {
            return tl::unexpected(
                OPK_ERROR(opk::ErrorFlag::InvalidData,
                          "Yolo26InstanceSegmentationParser: prototype values must be finite"));
        }
    }

    const auto &image = input.inferenceInfo.image;
    const auto activeWidth = ModelSize - image.letterboxLeft - image.letterboxRight;
    const auto activeHeight = ModelSize - image.letterboxTop - image.letterboxBottom;
    std::vector<Candidate> candidates;
    for (std::size_t row = 0U; row < CandidateCount; ++row) {
        const auto base = row * RowSize;
        const float score = detections.get(base + 4U);
        if (!std::isfinite(score) || score < 0.0F || score > 1.0F) {
            return tl::unexpected(OPK_ERROR(
                opk::ErrorFlag::InvalidData,
                "Yolo26InstanceSegmentationParser: detection scores must be finite and in [0,1]"));
        }
        if (score <= threshold) {
            continue;
        }
        for (std::size_t column = 0U; column < RowSize; ++column) {
            if (!std::isfinite(detections.get(base + column))) {
                return tl::unexpected(OPK_ERROR(
                    opk::ErrorFlag::InvalidData,
                    "Yolo26InstanceSegmentationParser: retained detection rows must be finite"));
            }
        }
        const float classId = detections.get(base + 5U);
        if (classId < 0.0F || classId > 79.0F || std::trunc(classId) != classId) {
            return tl::unexpected(OPK_ERROR(
                opk::ErrorFlag::InvalidData,
                "Yolo26InstanceSegmentationParser: class IDs must be integers in [0,79]"));
        }
        Candidate candidate;
        candidate.confidence = score;
        candidate.classId = static_cast<std::int32_t>(classId);
        candidate.box = {
            restoreCoordinate(detections.get(base), image.letterboxLeft, activeWidth, image.width),
            restoreCoordinate(
                detections.get(base + 1U), image.letterboxTop, activeHeight, image.height),
            restoreCoordinate(
                detections.get(base + 2U), image.letterboxLeft, activeWidth, image.width),
            restoreCoordinate(
                detections.get(base + 3U), image.letterboxTop, activeHeight, image.height)};
        for (std::size_t channel = 0U; channel < PrototypeCount; ++channel) {
            candidate.coefficients[channel] = detections.get(base + 6U + channel);
        }
        candidates.push_back(candidate);
    }

    open_perception_kit::metadata::InstanceSegmentationMasksT payload;
    payload.layer =
        open_perception_kit::makeLayerInfo({.model = input.inferenceInfo.modelName,
                                            .inferElementId = input.inferenceInfo.inferElementId,
                                            .contentType = k_content_type,
                                            .labelFamily = "coco",
                                            .producer = &input.producerInfo});
    if (!candidates.empty()) {
        const auto xSamples = makeAxisSamples(image.width, activeWidth, image.letterboxLeft);
        const auto ySamples = makeAxisSamples(image.height, activeHeight, image.letterboxTop);
        std::vector<float> logits(PrototypePixels);
        const auto bitmapBytes = image.width * image.height;
        std::size_t payloadBytes = 0U;
        for (const auto &candidate : candidates) {
            const auto &box = candidate.box;
            if (box.right <= box.left || box.bottom <= box.top) {
                continue;
            }
            std::fill(logits.begin(), logits.end(), 0.0F);
            for (std::size_t channel = 0U; channel < PrototypeCount; ++channel) {
                const float coefficient = candidate.coefficients[channel];
                for (std::size_t pixel = 0U; pixel < PrototypePixels; ++pixel) {
                    logits[pixel] +=
                        coefficient * prototypeValues[channel * PrototypePixels + pixel];
                }
            }
            if (!std::all_of(logits.begin(), logits.end(), [](float value) {
                    return std::isfinite(value);
                })) {
                return tl::unexpected(OPK_ERROR(
                    opk::ErrorFlag::InvalidData,
                    "Yolo26InstanceSegmentationParser: reconstructed mask logits must be finite"));
            }
            opk::Bitmap bitmap(opk::Bitmap::Type::Uint8, image.width, image.height);
            if (!fillMask(bitmap, logits, xSamples, ySamples, box)) {
                continue;
            }
            if (bitmapBytes > MaxMaskBytes - payloadBytes) {
                return tl::unexpected(OPK_ERROR(
                    opk::ErrorFlag::InvalidData,
                    "Yolo26InstanceSegmentationParser: instance masks exceed packet size limits"));
            }
            payloadBytes += bitmapBytes;
            auto mask =
                std::make_unique<open_perception_kit::metadata::InstanceSegmentationMaskT>();
            mask->object = open_perception_kit::makeObjectMeta(0U, input.inferenceInfo.parentId);
            mask->bitmap = open_perception_kit::makeBitmapData(bitmap);
            mask->class_id = candidate.classId;
            mask->confidence = candidate.confidence;
            mask->box = open_perception_kit::makeBoundingBox(
                box.left, box.top, box.right - box.left, box.bottom - box.top);
            payload.masks.push_back(std::move(mask));
        }
    }
    results.add(std::move(payload));
    return {};
}

} // namespace opk::stdop::postproc
