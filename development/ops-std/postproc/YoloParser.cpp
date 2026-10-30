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

#include "postproc/YoloParser.h"
#include "Log.h"
#include "opk/Labels.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace opk::stdop::postproc {
namespace {

struct Detection {
    float left, top, right, bottom, confidence;
    int classId;
};

struct Anchor {
    size_t row;
    float confidence;
};

float intersectionOverUnion(const Detection &first, const Detection &second) {
    const float left = std::max(first.left, second.left);
    const float top = std::max(first.top, second.top);
    const float right = std::min(first.right, second.right);
    const float bottom = std::min(first.bottom, second.bottom);
    const float overlapWidth = std::max(0.f, right - left);
    const float overlapHeight = std::max(0.f, bottom - top);
    const float intersectionArea = overlapWidth * overlapHeight;
    const float unionArea = (first.right - first.left) * (first.bottom - first.top) +
                            (second.right - second.left) * (second.bottom - second.top) -
                            intersectionArea;
    return unionArea > 0 ? intersectionArea / unionArea : 0.f;
}

void suppressOverlappingDetections(std::vector<Detection> &detections, const float iouThreshold) {
    std::sort(detections.begin(), detections.end(), [](const auto &first, const auto &second) {
        return first.confidence > second.confidence;
    });

    std::vector<char> suppressed(detections.size());
    std::vector<Detection> kept;
    kept.reserve(detections.size());
    for (size_t index = 0; index < detections.size(); ++index) {
        if (suppressed[index]) {
            continue;
        }
        kept.push_back(detections[index]);
        for (size_t other = index + 1; other < detections.size(); ++other) {
            if (!suppressed[other] &&
                intersectionOverUnion(detections[index], detections[other]) > iouThreshold) {
                suppressed[other] = 1;
            }
        }
    }
    detections.swap(kept);
}

float clampCoordinate(const float value, const float lower, const float upper) {
    return std::max(lower, std::min(value, upper));
}

size_t activeModelWidth(const opk::ImageInferenceMetadata &image) {
    const size_t horizontalPadding = image.letterboxLeft + image.letterboxRight;
    if (horizontalPadding >= image.modelWidth) {
        return image.modelWidth;
    }
    return image.modelWidth - horizontalPadding;
}

size_t activeModelHeight(const opk::ImageInferenceMetadata &image) {
    const size_t verticalPadding = image.letterboxTop + image.letterboxBottom;
    if (verticalPadding >= image.modelHeight) {
        return image.modelHeight;
    }
    return image.modelHeight - verticalPadding;
}

float modelToFrameX(float x, const opk::ImageInferenceMetadata &image) {
    return (x - static_cast<float>(image.letterboxLeft)) * static_cast<float>(image.width) /
           static_cast<float>(activeModelWidth(image));
}

float modelToFrameY(float y, const opk::ImageInferenceMetadata &image) {
    return (y - static_cast<float>(image.letterboxTop)) * static_cast<float>(image.height) /
           static_cast<float>(activeModelHeight(image));
}

void appendDetections(const std::vector<Detection> &detections,
                      const opk::TensorParser::Input &input,
                      open_perception_kit::metadata::BoxDetectionsT &detectionResult,
                      bool normalizeOutputCoordinates) {
    const float frameWidth = static_cast<float>(input.inferenceInfo.image.width);
    const float frameHeight = static_cast<float>(input.inferenceInfo.image.height);

    for (const auto &candidate : detections) {
        float x = candidate.left;
        float y = candidate.top;
        float width = candidate.right - candidate.left;
        float height = candidate.bottom - candidate.top;

        if (normalizeOutputCoordinates) {
            x /= frameWidth;
            width /= frameWidth;
            y /= frameHeight;
            height /= frameHeight;
        }

        auto detection = std::make_unique<open_perception_kit::metadata::BoxDetectionT>();
        detection->object = open_perception_kit::makeObjectMeta(0U, input.inferenceInfo.parentId);
        detection->box = open_perception_kit::makeBoundingBox(x, y, width, height);
        detection->confidence = candidate.confidence;
        detection->class_id = candidate.classId;
        detection->text =
            opk::resources::Labels::getLabel(opk::resources::LabelType::Coco, candidate.classId);
        detectionResult.detections.push_back(std::move(detection));
    }
}

void restoreFrameCoordinates(std::vector<Detection> &detections,
                             const opk::TensorParser::Input &input) {
    const auto &image = input.inferenceInfo.image;
    const bool coordinatesAreNormalized =
        input.attributes.getBoolOrDefault("coordinatesAreNormalized", false);

    for (auto &detection : detections) {
        if (coordinatesAreNormalized) {
            detection.left *= static_cast<float>(image.modelWidth);
            detection.right *= static_cast<float>(image.modelWidth);
            detection.top *= static_cast<float>(image.modelHeight);
            detection.bottom *= static_cast<float>(image.modelHeight);
        }

        detection.left = modelToFrameX(detection.left, image);
        detection.right = modelToFrameX(detection.right, image);
        detection.top = modelToFrameY(detection.top, image);
        detection.bottom = modelToFrameY(detection.bottom, image);

        detection.left = clampCoordinate(detection.left, 0.0f, static_cast<float>(image.width - 1));
        detection.right =
            clampCoordinate(detection.right, 0.0f, static_cast<float>(image.width - 1));
        detection.top = clampCoordinate(detection.top, 0.0f, static_cast<float>(image.height - 1));
        detection.bottom =
            clampCoordinate(detection.bottom, 0.0f, static_cast<float>(image.height - 1));
    }
}

std::vector<Detection> parseCenterClassScores(const opk::TensorParser::Input &input,
                                              const TensorView &tensor,
                                              const opk::Shape &shape) {
    const auto confidenceThreshold =
        static_cast<float>(input.attributes.getDoubleOrDefault("confidenceThreshold", 0.25));
    const bool channelsFirst = shape.dims[1] <= shape.dims[2];
    const size_t channels = channelsFirst ? shape.dims[1] : shape.dims[2];
    const size_t candidates = channelsFirst ? shape.dims[2] : shape.dims[1];

    std::vector<Detection> detections;
    detections.reserve(candidates);
    for (size_t index = 0; index < candidates; ++index) {
        const size_t base = channelsFirst ? index : index * channels;
        const size_t stride = channelsFirst ? candidates : 1;
        const auto value = [&](size_t channel) { return tensor.get(base + channel * stride); };

        int bestClass = -1;
        float bestScore = 0.0f;
        for (size_t channel = 4; channel < channels; ++channel) {
            if (const float score = value(channel); score > bestScore) {
                bestScore = score;
                bestClass = static_cast<int>(channel - 4);
            }
        }
        if (bestScore < confidenceThreshold) {
            continue;
        }

        const float centerX = value(0);
        const float centerY = value(1);
        const float width = value(2);
        const float height = value(3);
        Detection detection{centerX - width * 0.5f,
                            centerY - height * 0.5f,
                            centerX + width * 0.5f,
                            centerY + height * 0.5f,
                            bestScore,
                            bestClass};
        detections.push_back(detection);
    }
    return detections;
}

opk::Result<std::vector<Anchor>> selectTopAnchors(const TensorView &tensor,
                                                  const opk::Shape &shape,
                                                  const float confidenceThreshold,
                                                  const size_t maxDetections) {
    const size_t candidates = shape.dims[1];
    const size_t channels = shape.dims[2];
    std::vector<Anchor> anchors;
    for (size_t row = 0; row < candidates; ++row) {
        float bestScore = -std::numeric_limits<float>::infinity();
        for (size_t column = 4; column < channels; ++column) {
            const float score = tensor.get(row * channels + column);
            if (!std::isfinite(score)) {
                return tl::unexpected(
                    OPK_ERROR(ErrorFlag::InvalidData, "YoloParser: non-finite class score"));
            }
            bestScore = std::max(bestScore, score);
        }
        if (bestScore >= confidenceThreshold) {
            anchors.push_back({row, bestScore});
        }
    }
    std::stable_sort(anchors.begin(), anchors.end(), [](const auto &first, const auto &second) {
        return first.confidence > second.confidence;
    });
    anchors.resize(std::min(anchors.size(), maxDetections));
    return anchors;
}

opk::Result<std::vector<Detection>> selectTopClasses(const TensorView &tensor,
                                                     const opk::Shape &shape,
                                                     const std::vector<Anchor> &anchors,
                                                     const float confidenceThreshold,
                                                     const size_t maxDetections) {
    const size_t channels = shape.dims[2];
    std::vector<Detection> detections;
    for (const auto &anchor : anchors) {
        const size_t base = anchor.row * channels;
        for (size_t column = 4; column < channels; ++column) {
            const float score = tensor.get(base + column);
            if (score < confidenceThreshold) {
                continue;
            }
            const Detection detection{tensor.get(base),
                                      tensor.get(base + 1),
                                      tensor.get(base + 2),
                                      tensor.get(base + 3),
                                      score,
                                      static_cast<int>(column - 4)};
            if (!std::isfinite(detection.left) || !std::isfinite(detection.top) ||
                !std::isfinite(detection.right) || !std::isfinite(detection.bottom)) {
                return tl::unexpected(
                    OPK_ERROR(ErrorFlag::InvalidData, "YoloParser: non-finite box"));
            }
            detections.push_back(detection);
        }
    }
    std::stable_sort(
        detections.begin(), detections.end(), [](const auto &first, const auto &second) {
            return first.confidence > second.confidence;
        });
    detections.resize(std::min(detections.size(), maxDetections));
    return detections;
}

opk::Result<std::vector<Detection>> parseCornerClassScores(const opk::TensorParser::Input &input,
                                                           const TensorView &tensor,
                                                           const opk::Shape &shape) {
    const auto limit = input.attributes.getIntOrDefault("maxDetections", 300);
    if (limit <= 0 || shape.dims[2] < 5) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData,
                      "YoloParser: cornerClassScores needs [1,N,4+C] and positive maxDetections"));
    }
    const size_t maxDetections = static_cast<size_t>(limit);
    const float confidenceThreshold =
        static_cast<float>(input.attributes.getDoubleOrDefault("confidenceThreshold", 0.25));
    // The export selects anchors first, then class/anchor pairs. Both stages preserve ties.
    const auto anchors = selectTopAnchors(tensor, shape, confidenceThreshold, maxDetections);
    if (!anchors) {
        return tl::unexpected(anchors.error());
    }
    return selectTopClasses(tensor, shape, *anchors, confidenceThreshold, maxDetections);
}

std::vector<Detection> parseCornerScoreClass(const opk::TensorParser::Input &input,
                                             const TensorView &tensor,
                                             const opk::Shape &shape) {
    const auto confidenceThreshold =
        static_cast<float>(input.attributes.getDoubleOrDefault("confidenceThreshold", 0.25));
    const size_t candidates = shape.dims[1];

    std::vector<Detection> detections;
    detections.reserve(candidates);
    for (size_t index = 0; index < candidates; ++index) {
        const size_t base = index * 6U;
        const float topLeftX = tensor.get(base);
        const float topLeftY = tensor.get(base + 1U);
        const float bottomRightX = tensor.get(base + 2U);
        const float bottomRightY = tensor.get(base + 3U);
        const float confidence = tensor.get(base + 4U);
        const float classId = tensor.get(base + 5U);

        if (confidence < confidenceThreshold) {
            continue;
        }

        Detection detection{
            topLeftX, topLeftY, bottomRightX, bottomRightY, confidence, static_cast<int>(classId)};
        detections.push_back(detection);
    }
    return detections;
}

opk::Result<void> validateInput(const opk::TensorParser::Input &input,
                                const std::string &outputFormat) {
    if (outputFormat != "centerClassScores" && outputFormat != "cornerScoreClass" &&
        outputFormat != "cornerClassScores") {
        return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData,
                                        "YoloParser: unsupported outputFormat " + outputFormat));
    }
    const auto &image = input.inferenceInfo.image;
    const size_t frameWidth = image.width;
    const size_t frameHeight = image.height;

    const size_t modelWidth = image.modelWidth;
    const size_t modelHeight = image.modelHeight;

    if (frameWidth == 0 || frameHeight == 0 || modelWidth == 0 || modelHeight == 0) {
        opk::log::error("YoloParser: image dimensions must be positive, got frame {}x{} and model "
                        "{}x{}\n",
                        frameWidth,
                        frameHeight,
                        modelWidth,
                        modelHeight);
        return tl::unexpected(OPK_ERROR(
            opk::ErrorFlag::InvalidData,
            "YoloParser: image dimensions must be positive, got frame " +
                std::to_string(frameWidth) + "x" + std::to_string(frameHeight) + " and model " +
                std::to_string(modelWidth) + "x" + std::to_string(modelHeight)));
    }

    if (!input.tensors[0]) {
        opk::log::error("YoloParser: input tensor is null\n");
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData, "YoloParser: input tensor is null"));
    }

    const TensorView &tensor = *input.tensors[0];
    const opk::Shape shape = input.tensors[0]->getShape();
    if (shape.rank != 3 || shape.dims[0] != 1 || shape.dims[1] <= 0 || shape.dims[2] <= 0) {
        opk::log::error("YoloParser: tensor must be 3D with shape [1,C,N] or [1,N,C], got {}\n",
                        shape.toString());
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "YoloParser: tensor must be 3D with shape [1,C,N] or [1,N,C], got " +
                          shape.toString()));
    }

    if (outputFormat == "cornerScoreClass" && shape.dims[2] != 6) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "YoloParser: cornerScoreClass tensor must have shape [1,N,6], got " +
                          shape.toString()));
    }

    if (outputFormat == "centerClassScores" && std::min(shape.dims[1], shape.dims[2]) < 5) {
        opk::log::error("YoloParser: tensor needs at least 5 values per candidate, got {}\n",
                        shape.toString());
        return tl::unexpected(OPK_ERROR(
            opk::ErrorFlag::InvalidData,
            "YoloParser: tensor needs at least 5 values per candidate, got " + shape.toString()));
    }

    if (!tensor.isValid()) {
        opk::log::error("YoloParser: input tensor view is invalid\n");
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData, "YoloParser: input tensor view is invalid"));
    }

    return {};
}

opk::Result<std::vector<Detection>> readDetections(const opk::TensorParser::Input &input) {
    const auto outputFormat =
        input.attributes.getStringOrDefault("outputFormat", "centerClassScores");
    const auto validation = validateInput(input, outputFormat);
    if (!validation) {
        return tl::unexpected(validation.error());
    }
    const TensorView &tensor = *input.tensors[0];
    const opk::Shape shape = tensor.getShape();
    if (outputFormat == "centerClassScores") {
        return parseCenterClassScores(input, tensor, shape);
    }
    if (outputFormat == "cornerScoreClass") {
        return parseCornerScoreClass(input, tensor, shape);
    }
    return parseCornerClassScores(input, tensor, shape);
}

} // namespace

opk::Result<void> YoloParser::parse(const opk::TensorParser::Input &input,
                                    open_perception_kit::FrameResults &results) {
    const auto iouThreshold =
        static_cast<float>(input.attributes.getDoubleOrDefault("iouThreshold", 0.45));
    const bool normalizeOutputCoordinates =
        input.attributes.getBoolOrDefault("normalizeOutputCoordinates", true);
    const bool applyNms = input.attributes.getBoolOrDefault("applyNms", true);

    auto detections = readDetections(input);
    if (!detections) {
        return tl::unexpected(detections.error());
    }
    if (!detections->empty()) {
        restoreFrameCoordinates(*detections, input);
        if (applyNms) {
            suppressOverlappingDetections(*detections, iouThreshold);
        }
        open_perception_kit::metadata::BoxDetectionsT payload;
        payload.layer = open_perception_kit::makeLayerInfo(
            {.model = input.inferenceInfo.modelName,
             .inferElementId = input.inferenceInfo.inferElementId,
             .contentType = k_content_type,
             .labelFamily = "coco",
             .producer = &input.producerInfo});
        appendDetections(*detections, input, payload, normalizeOutputCoordinates);
        results.add(std::move(payload));
    }
    return {};
}

} // namespace opk::stdop::postproc
