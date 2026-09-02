/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "postproc/YoloParser.h"
#include "pek/Labels.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

using namespace pek;
using namespace pek::stdop::postproc;

struct Det {
    float x1, y1, x2, y2, conf;
    int cls;
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

static inline size_t activeModelWidth(const pek::ImageInferenceMetadata &image) {
    const size_t horizontalPadding = image.letterboxLeft + image.letterboxRight;
    if (horizontalPadding >= image.modelWidth) {
        return image.modelWidth;
    }
    return image.modelWidth - horizontalPadding;
}

static inline size_t activeModelHeight(const pek::ImageInferenceMetadata &image) {
    const size_t verticalPadding = image.letterboxTop + image.letterboxBottom;
    if (verticalPadding >= image.modelHeight) {
        return image.modelHeight;
    }
    return image.modelHeight - verticalPadding;
}

static inline float modelToFrameX(float x, const pek::ImageInferenceMetadata &image) {
    return (x - static_cast<float>(image.letterboxLeft)) * static_cast<float>(image.width) /
           static_cast<float>(activeModelWidth(image));
}

static inline float modelToFrameY(float y, const pek::ImageInferenceMetadata &image) {
    return (y - static_cast<float>(image.letterboxTop)) * static_cast<float>(image.height) /
           static_cast<float>(activeModelHeight(image));
}

static void fillDetection(const std::vector<Det> &dets,
                          const pek::TensorParser::Input &input,
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
        detection->text = pek::resources::Labels::getLabel(pek::resources::LabelType::Coco, a.cls);
        detectionResult.detections.push_back(std::move(detection));
    }
}

static void processDetection(const pek::TensorParser::Input &input,
                             Det &d,
                             const pek::ImageInferenceMetadata &image) {
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

static std::vector<Det> parseUltralyticsDetections(const pek::TensorParser::Input &input,
                                                   const TensorView &tensor,
                                                   const pek::Shape &shape) {
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

// ----------------------------------------------------------------------------

pek::Result<void> YoloParser::parse(const pek::TensorParser::Input &input,
                                    perception::FrameResults &results) {

    const auto iouThreshold =
        static_cast<float>(input.attributes.getDoubleOrDefault("iouThreshold", 0.45));
    const bool normalizeOutputCoordinates =
        input.attributes.getBoolOrDefault("normalizeOutputCoordinates", true);
    const bool applyNms = input.attributes.getBoolOrDefault("applyNms", true);

    const auto &image = input.inferenceInfo.image;
    const size_t frameWidth = image.width;
    const size_t frameHeight = image.height;

    const size_t modelWidth = image.modelWidth;
    const size_t modelHeight = image.modelHeight;

    assert(frameWidth != 0);
    assert(frameHeight != 0);
    assert(modelWidth != 0);
    assert(modelHeight != 0);
    assert(input.tensors[0]);
    assert(input.inferenceInfo.image.modelWidth == input.inferenceInfo.image.modelHeight);

    const TensorView &tensor = *input.tensors[0];
    const pek::Shape shape = input.tensors[0]->getShape();

    if (auto dets = parseUltralyticsDetections(input, tensor, shape); !dets.empty()) {
        if (applyNms)
            nms(dets, iouThreshold);

        perception::metadata::BoxDetectionsT payload;
        payload.layer = perception::makeLayerInfo(input.inferenceInfo.modelName,
                                                  input.inferenceInfo.inferElementId,
                                                  k_content_type,
                                                  "",
                                                  "",
                                                  "coco");
        fillDetection(dets, input, payload, normalizeOutputCoordinates);
        if (!payload.detections.empty()) {
            results.add(std::move(payload));
        }
    }

    return {};
}
