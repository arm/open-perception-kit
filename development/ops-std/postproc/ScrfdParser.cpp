/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "postproc/ScrfdParser.h"

#include "pek/FrameResults.h"

#include <algorithm>
#include <cmath>
#include <fmt/core.h>
#include <map>
#include <memory>
#include <utility>
#include <vector>

using namespace pek;
using FaceDetection = perception::metadata::BoxDetectionT;

namespace {

inline float iou(const FaceDetection &a, const FaceDetection &b) {
    const auto &abox = *a.box;
    const auto &bbox = *b.box;
    const float ax2 = abox.x + abox.width;
    const float ay2 = abox.y + abox.height;
    const float bx2 = bbox.x + bbox.width;
    const float by2 = bbox.y + bbox.height;

    const float interLeft = std::max(abox.x, bbox.x);
    const float interTop = std::max(abox.y, bbox.y);
    const float interRight = std::min(ax2, bx2);
    const float interBottom = std::min(ay2, by2);

    const float interW = interRight - interLeft;
    const float interH = interBottom - interTop;
    if (interW <= 0.0f || interH <= 0.0f)
        return 0.0f;

    const float interArea = interW * interH;
    const float unionArea = abox.width * abox.height + bbox.width * bbox.height - interArea;
    if (unionArea <= 0.0f)
        return 0.0f;

    return interArea / unionArea;
}

std::vector<FaceDetection> nonMaxSuppression(const std::vector<FaceDetection> &detections,
                                             float scoreThreshold,
                                             float iouThreshold,
                                             size_t maxDetections) {
    std::vector<FaceDetection> candidates;
    candidates.reserve(detections.size());
    for (const auto &det : detections) {
        if (det.confidence >= scoreThreshold) {
            candidates.push_back(det);
        }
    }

    std::ranges::sort(candidates, [](const FaceDetection &lhs, const FaceDetection &rhs) {
        return lhs.confidence > rhs.confidence;
    });

    std::vector<FaceDetection> result;
    std::vector<bool> suppressed(candidates.size(), false);
    result.reserve(std::min(maxDetections, candidates.size()));

    for (size_t i = 0; i < candidates.size() && result.size() < maxDetections; ++i) {
        if (suppressed[i])
            continue;

        const auto &current = candidates[i];
        result.push_back(current);

        for (size_t j = i + 1; j < candidates.size(); ++j) {
            if (suppressed[j])
                continue;
            if (iou(current, candidates[j]) >= iouThreshold) {
                suppressed[j] = true;
            }
        }
    }

    return result;
}

struct FeatureMapGroup {
    const TensorView *scores = nullptr;
    const TensorView *boxes = nullptr;
    const TensorView *landmarks = nullptr;
    size_t height = 0;
    size_t width = 0;
    size_t stride = 0;
    size_t anchorsPerCell = 0;
};

inline float nhwcAt(const TensorView &tensor, size_t y, size_t x, size_t c) {
    const auto shape = tensor.getShape();
    const size_t width = static_cast<size_t>(shape.dims[2]);
    const size_t channels = static_cast<size_t>(shape.dims[3]);
    return tensor.get((y * width + x) * channels + c);
}

static pek::Result<std::vector<FeatureMapGroup>>
validateGroups(const pek::TensorParser::Input &input, size_t modelWidth, size_t modelHeight) {
    std::map<size_t, FeatureMapGroup> groups;

    for (size_t i = 0; i < pek::MaxTensorCount; ++i) {
        const TensorView *tensor = input.tensors[i];
        if (!tensor)
            continue;

        const auto shape = tensor->getShape();
        if (shape.rank != 4) {
            return tl::unexpected(PEK_ERROR(
                ErrorFlag::InvalidData,
                fmt::format(
                    "ScrfdParser: tensor {} must be NHWC [1,H,W,C], got {}", i, shape.toString())));
        }
        if (shape.dims[0] != 1) {
            return tl::unexpected(PEK_ERROR(
                ErrorFlag::InvalidData,
                fmt::format("ScrfdParser: tensor {} batch must be 1, got {}", i, shape.dims[0])));
        }

        const size_t height = static_cast<size_t>(shape.dims[1]);
        const size_t width = static_cast<size_t>(shape.dims[2]);
        const size_t channels = static_cast<size_t>(shape.dims[3]);

        if (height == 0 || width == 0 || modelWidth % width != 0 || modelHeight % height != 0) {
            return tl::unexpected(
                PEK_ERROR(ErrorFlag::InvalidData,
                          fmt::format("ScrfdParser: tensor {} shape {} does not map cleanly to "
                                      "model size {}x{}",
                                      i,
                                      shape.toString(),
                                      modelWidth,
                                      modelHeight)));
        }

        const size_t strideX = modelWidth / width;
        const size_t strideY = modelHeight / height;
        if (strideX != strideY) {
            return tl::unexpected(
                PEK_ERROR(ErrorFlag::InvalidData,
                          fmt::format("ScrfdParser: tensor {} shape {} implies non-square stride "
                                      "{}x{}",
                                      i,
                                      shape.toString(),
                                      strideX,
                                      strideY)));
        }

        auto &group = groups[strideX];
        group.height = height;
        group.width = width;
        group.stride = strideX;

        if (channels % 10 == 0) {
            if (group.landmarks) {
                return tl::unexpected(PEK_ERROR(
                    ErrorFlag::InvalidData,
                    fmt::format("ScrfdParser: duplicate landmark tensor for stride {}", strideX)));
            }
            group.landmarks = tensor;
            group.anchorsPerCell = channels / 10;
        } else if (channels % 4 == 0) {
            if (group.boxes) {
                return tl::unexpected(PEK_ERROR(
                    ErrorFlag::InvalidData,
                    fmt::format("ScrfdParser: duplicate box tensor for stride {}", strideX)));
            }
            group.boxes = tensor;
            group.anchorsPerCell = channels / 4;
        } else {
            if (group.scores) {
                return tl::unexpected(PEK_ERROR(
                    ErrorFlag::InvalidData,
                    fmt::format("ScrfdParser: duplicate score tensor for stride {}", strideX)));
            }
            group.scores = tensor;
            group.anchorsPerCell = channels;
        }
    }

    if (groups.empty()) {
        return tl::unexpected(
            PEK_ERROR(ErrorFlag::InvalidData, "ScrfdParser: no output tensors were provided"));
    }

    std::vector<FeatureMapGroup> ordered;
    ordered.reserve(groups.size());
    for (auto &[stride, group] : groups) {
        (void)stride;
        if (!group.scores || !group.boxes) {
            return tl::unexpected(
                PEK_ERROR(ErrorFlag::InvalidData,
                          fmt::format("ScrfdParser: missing score or box tensor for stride {}",
                                      group.stride)));
        }

        const size_t scoreAnchors = static_cast<size_t>(group.scores->getShape().dims[3]);
        const size_t boxAnchors = static_cast<size_t>(group.boxes->getShape().dims[3]) / 4;
        if (scoreAnchors != boxAnchors) {
            return tl::unexpected(PEK_ERROR(
                ErrorFlag::InvalidData,
                fmt::format("ScrfdParser: score/box anchor mismatch at stride {}: {} vs {}",
                            group.stride,
                            scoreAnchors,
                            boxAnchors)));
        }
        if (group.landmarks) {
            const size_t kpsAnchors = static_cast<size_t>(group.landmarks->getShape().dims[3]) / 10;
            if (kpsAnchors != scoreAnchors) {
                return tl::unexpected(PEK_ERROR(
                    ErrorFlag::InvalidData,
                    fmt::format("ScrfdParser: landmark anchor mismatch at stride {}: {} vs {}",
                                group.stride,
                                kpsAnchors,
                                scoreAnchors)));
            }
        }

        group.anchorsPerCell = scoreAnchors;
        ordered.push_back(group);
    }

    std::sort(
        ordered.begin(), ordered.end(), [](const FeatureMapGroup &a, const FeatureMapGroup &b) {
            return a.stride < b.stride;
        });

    return ordered;
}

} // namespace

pek::Result<void> pek::stdop::postproc::ScrfdParser::parse(const pek::TensorParser::Input &input,
                                                           perception::FrameResults &results) {
    const float confThreshold =
        static_cast<float>(input.attributes.getDoubleOrDefault("confidenceThreshold", 0.5));
    const float iouThreshold =
        static_cast<float>(input.attributes.getDoubleOrDefault("iouThreshold", 0.4));
    const bool normalizeOutputCoordinates =
        input.attributes.getBoolOrDefault("normalizeOutputCoordinates", false);
    const size_t maxDetections =
        static_cast<size_t>(input.attributes.getIntOrDefault("maxDetections", 100));

    const size_t modelWidth = input.inferenceInfo.image.modelWidth;
    const size_t modelHeight = input.inferenceInfo.image.modelHeight;
    const size_t frameWidth = input.inferenceInfo.image.width;
    const size_t frameHeight = input.inferenceInfo.image.height;

    if (modelWidth == 0 || modelHeight == 0 || frameWidth == 0 || frameHeight == 0) {
        return tl::unexpected(
            PEK_ERROR(ErrorFlag::InvalidData,
                      fmt::format("ScrfdParser: invalid image dimensions frame={}x{}, model={}x{}",
                                  frameWidth,
                                  frameHeight,
                                  modelWidth,
                                  modelHeight)));
    }

    auto groupsResult = validateGroups(input, modelWidth, modelHeight);
    if (!groupsResult) {
        return tl::unexpected(groupsResult.error());
    }

    const float scaleX = static_cast<float>(frameWidth) / static_cast<float>(modelWidth);
    const float scaleY = static_cast<float>(frameHeight) / static_cast<float>(modelHeight);
    std::vector<FaceDetection> detections;

    for (const auto &group : *groupsResult) {
        for (size_t y = 0; y < group.height; ++y) {
            for (size_t x = 0; x < group.width; ++x) {
                for (size_t anchor = 0; anchor < group.anchorsPerCell; ++anchor) {
                    const float score = nhwcAt(*group.scores, y, x, anchor);
                    if (score < confThreshold) {
                        continue;
                    }

                    const size_t boxBase = anchor * 4;
                    const float l =
                        nhwcAt(*group.boxes, y, x, boxBase + 0) * static_cast<float>(group.stride);
                    const float t =
                        nhwcAt(*group.boxes, y, x, boxBase + 1) * static_cast<float>(group.stride);
                    const float r =
                        nhwcAt(*group.boxes, y, x, boxBase + 2) * static_cast<float>(group.stride);
                    const float b =
                        nhwcAt(*group.boxes, y, x, boxBase + 3) * static_cast<float>(group.stride);

                    const float anchorCenterX = static_cast<float>(x * group.stride);
                    const float anchorCenterY = static_cast<float>(y * group.stride);

                    float x1 = std::clamp(anchorCenterX - l, 0.0f, static_cast<float>(modelWidth));
                    float y1 = std::clamp(anchorCenterY - t, 0.0f, static_cast<float>(modelHeight));
                    float x2 = std::clamp(anchorCenterX + r, 0.0f, static_cast<float>(modelWidth));
                    float y2 = std::clamp(anchorCenterY + b, 0.0f, static_cast<float>(modelHeight));

                    x1 *= scaleX;
                    y1 *= scaleY;
                    x2 *= scaleX;
                    y2 *= scaleY;

                    if (normalizeOutputCoordinates) {
                        x1 /= static_cast<float>(frameWidth);
                        y1 /= static_cast<float>(frameHeight);
                        x2 /= static_cast<float>(frameWidth);
                        y2 /= static_cast<float>(frameHeight);
                    }

                    FaceDetection detection;
                    detection.object = perception::makeObjectMeta(0U, input.inferenceInfo.parentId);
                    detection.box = perception::makeBoundingBox(
                        x1, y1, std::max(0.0f, x2 - x1), std::max(0.0f, y2 - y1));
                    detection.confidence = score;
                    detection.class_id = 0;
                    detections.push_back(std::move(detection));
                }
            }
        }
    }

    detections = nonMaxSuppression(detections, confThreshold, iouThreshold, maxDetections);

    perception::metadata::BoxDetectionsT payload;
    payload.layer = perception::makeLayerInfo(
        input.inferenceInfo.modelName,
        input.inferenceInfo.inferElementId,
        k_content_type,
        "",
        "",
        "",
        "",
        &input.producerInfo);
    for (auto &detection : detections) {
        payload.detections.push_back(std::make_unique<FaceDetection>(std::move(detection)));
    }
    if (!payload.detections.empty()) {
        results.add(std::move(payload));
    }

    return {};
}
