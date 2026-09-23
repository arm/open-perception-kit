/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "postproc/YoloParser.h"
#include "Log.h"
#include "opk/Labels.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

using namespace opk;
using namespace opk::stdop::postproc;

struct Det {
    float x1, y1, x2, y2, conf;
    int cls;
};

enum class OutputFormat {
    CenterClassScores,
    CornerScoreClass,
};

static float iou(const Det &a, const Det &b) {
    float xx1 = std::max(a.x1, b.x1), yy1 = std::max(a.y1, b.y1);
    float xx2 = std::min(a.x2, b.x2), yy2 = std::min(a.y2, b.y2);
    float w = std::max(0.f, xx2 - xx1), h = std::max(0.f, yy2 - yy1);
    float inter = w * h,
          uni = (a.x2 - a.x1) * (a.y2 - a.y1) + (b.x2 - b.x1) * (b.y2 - b.y1) - inter;
    return uni > 0 ? inter / uni : 0.f;
}
static void nms(std::vector<Det> &d, float iou_thr) {
    std::sort(d.begin(), d.end(), [](auto &a, auto &b) { return a.conf > b.conf; });

    std::vector<char> sup(d.size());
    std::vector<Det> keep;
    keep.reserve(d.size());
    for (size_t i = 0; i < d.size(); ++i) {
        if (sup[i])
            continue;
        keep.push_back(d[i]);
        for (size_t j = i + 1; j < d.size(); ++j)
            if (!sup[j] && iou(d[i], d[j]) > iou_thr)
                sup[j] = 1;
    }
    d.swap(keep);
}

static inline float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(v, hi));
}

static inline size_t activeModelWidth(const opk::ImageInferenceMetadata &image) {
    const size_t horizontalPadding = image.letterboxLeft + image.letterboxRight;
    if (horizontalPadding >= image.modelWidth) {
        return image.modelWidth;
    }
    return image.modelWidth - horizontalPadding;
}

static inline size_t activeModelHeight(const opk::ImageInferenceMetadata &image) {
    const size_t verticalPadding = image.letterboxTop + image.letterboxBottom;
    if (verticalPadding >= image.modelHeight) {
        return image.modelHeight;
    }
    return image.modelHeight - verticalPadding;
}

static inline float modelToFrameX(float x, const opk::ImageInferenceMetadata &image) {
    return (x - static_cast<float>(image.letterboxLeft)) * static_cast<float>(image.width) /
           static_cast<float>(activeModelWidth(image));
}

static inline float modelToFrameY(float y, const opk::ImageInferenceMetadata &image) {
    return (y - static_cast<float>(image.letterboxTop)) * static_cast<float>(image.height) /
           static_cast<float>(activeModelHeight(image));
}

static void fillDetection(const std::vector<Det> &dets,
                          const opk::TensorParser::Input &input,
                          perception::metadata::BoxDetectionsT &detectionResult,
                          bool normalizeOutputCoordinates) {
    const float frameWidth = static_cast<float>(input.inferenceInfo.image.width);
    const float frameHeight = static_cast<float>(input.inferenceInfo.image.height);

    for (const auto &a : dets) {
        float x = a.x1;
        float y = a.y1;
        float width = a.x2 - a.x1;
        float height = a.y2 - a.y1;

        if (normalizeOutputCoordinates) {
            x /= frameWidth;
            width /= frameWidth;
            y /= frameHeight;
            height /= frameHeight;
        }

        auto detection = std::make_unique<perception::metadata::BoxDetectionT>();
        detection->object = perception::makeObjectMeta(0U, input.inferenceInfo.parentId);
        detection->box = perception::makeBoundingBox(x, y, width, height);
        detection->confidence = a.conf;
        detection->class_id = a.cls;
        detection->text = opk::resources::Labels::getLabel(opk::resources::LabelType::Coco, a.cls);
        detectionResult.detections.push_back(std::move(detection));
    }
}

static void processDetection(const opk::TensorParser::Input &input,
                             Det &d,
                             const opk::ImageInferenceMetadata &image) {
    const bool coordinatesAreNormalized =
        input.attributes.getBoolOrDefault("coordinatesAreNormalized", false);

    if (coordinatesAreNormalized) {
        d.x1 *= static_cast<float>(image.modelWidth);
        d.x2 *= static_cast<float>(image.modelWidth);
        d.y1 *= static_cast<float>(image.modelHeight);
        d.y2 *= static_cast<float>(image.modelHeight);
    }

    d.x1 = modelToFrameX(d.x1, image);
    d.x2 = modelToFrameX(d.x2, image);
    d.y1 = modelToFrameY(d.y1, image);
    d.y2 = modelToFrameY(d.y2, image);

    d.x1 = clampf(d.x1, 0.0f, static_cast<float>(image.width - 1));
    d.x2 = clampf(d.x2, 0.0f, static_cast<float>(image.width - 1));
    d.y1 = clampf(d.y1, 0.0f, static_cast<float>(image.height - 1));
    d.y2 = clampf(d.y2, 0.0f, static_cast<float>(image.height - 1));
}

static std::vector<Det> parseCenterClassScoresDetections(const opk::TensorParser::Input &input,
                                                         const TensorView &tensor,
                                                         const opk::Shape &shape) {
    const auto confThreshold =
        static_cast<float>(input.attributes.getDoubleOrDefault("confidenceThreshold", 0.25));
    const bool channelsFirst = shape.dims[1] <= shape.dims[2];
    const size_t channels = channelsFirst ? shape.dims[1] : shape.dims[2];
    const size_t candidates = channelsFirst ? shape.dims[2] : shape.dims[1];

    std::vector<Det> detections;
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
        if (bestScore < confThreshold)
            continue;

        const float centerX = value(0);
        const float centerY = value(1);
        const float width = value(2);
        const float height = value(3);
        Det detection{centerX - width * 0.5f,
                      centerY - height * 0.5f,
                      centerX + width * 0.5f,
                      centerY + height * 0.5f,
                      bestScore,
                      bestClass};
        processDetection(input, detection, input.inferenceInfo.image);
        detections.push_back(detection);
    }
    return detections;
}

static std::vector<Det> parseCornerScoreClassDetections(const opk::TensorParser::Input &input,
                                                        const TensorView &tensor,
                                                        const opk::Shape &shape) {
    const auto confThreshold =
        static_cast<float>(input.attributes.getDoubleOrDefault("confidenceThreshold", 0.25));
    const size_t candidates = shape.dims[1];

    std::vector<Det> detections;
    detections.reserve(candidates);
    for (size_t index = 0; index < candidates; ++index) {
        const size_t base = index * 6U;
        const float topLeftX = tensor.get(base);
        const float topLeftY = tensor.get(base + 1U);
        const float bottomRightX = tensor.get(base + 2U);
        const float bottomRightY = tensor.get(base + 3U);
        const float confidence = tensor.get(base + 4U);
        const float classId = tensor.get(base + 5U);

        if (confidence < confThreshold) {
            continue;
        }

        Det detection{
            topLeftX, topLeftY, bottomRightX, bottomRightY, confidence, static_cast<int>(classId)};
        processDetection(input, detection, input.inferenceInfo.image);
        detections.push_back(detection);
    }
    return detections;
}

// ----------------------------------------------------------------------------

opk::Result<void> YoloParser::parse(const opk::TensorParser::Input &input,
                                    perception::FrameResults &results) {

    const auto iouThreshold =
        static_cast<float>(input.attributes.getDoubleOrDefault("iouThreshold", 0.45));
    const bool normalizeOutputCoordinates =
        input.attributes.getBoolOrDefault("normalizeOutputCoordinates", true);
    const bool applyNms = input.attributes.getBoolOrDefault("applyNms", true);
    const auto outputFormatAttribute =
        input.attributes.getStringOrDefault("outputFormat", "centerClassScores");
    OutputFormat outputFormat;
    if (outputFormatAttribute == "centerClassScores") {
        outputFormat = OutputFormat::CenterClassScores;
    } else if (outputFormatAttribute == "cornerScoreClass") {
        outputFormat = OutputFormat::CornerScoreClass;
    } else {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "YoloParser: unsupported outputFormat " + outputFormatAttribute));
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

    if (outputFormat == OutputFormat::CornerScoreClass && shape.dims[2] != 6) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "YoloParser: cornerScoreClass tensor must have shape [1,N,6], got " +
                          shape.toString()));
    }

    if (outputFormat == OutputFormat::CenterClassScores &&
        std::min(shape.dims[1], shape.dims[2]) < 5) {
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

    auto dets = outputFormat == OutputFormat::CornerScoreClass
                    ? parseCornerScoreClassDetections(input, tensor, shape)
                    : parseCenterClassScoresDetections(input, tensor, shape);
    if (!dets.empty()) {
        if (applyNms)
            nms(dets, iouThreshold);

        perception::metadata::BoxDetectionsT payload;
        payload.layer =
            perception::makeLayerInfo({.model = input.inferenceInfo.modelName,
                                       .inferElementId = input.inferenceInfo.inferElementId,
                                       .contentType = k_content_type,
                                       .labelFamily = "coco",
                                       .producer = &input.producerInfo});
        fillDetection(dets, input, payload, normalizeOutputCoordinates);
        if (!payload.detections.empty()) {
            results.add(std::move(payload));
        }
    }

    return {};
}
