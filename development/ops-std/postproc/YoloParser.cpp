/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "postproc/YoloParser.h"
#include "pek/Labels.h"
#include "pek/Perception.h"

#include <algorithm>
#include <cmath>
#include <fmt/core.h>
#include <string>
#include <vector>

using namespace pek;
using namespace pek::stdop::postproc;

struct Det {
    float x1, y1, x2, y2, conf;
    int cls;
};

enum class OutputFormat {
    UltralyticsYolo,
    HailoYoloNMS,
};

enum class CoordOrder {
    yxyx,
    xyxy,
    xywh,
};

static OutputFormat parseOutputFormat(const pek::AttributeMap &attrs) {
    const std::string fmt = attrs.getStringOrDefault("outputFormat", "UltralyticsYolo");

    if (fmt == "UltralyticsYolo") {
        return OutputFormat::UltralyticsYolo;
    }

    if (fmt == "HailoYoloNMS") {
        return OutputFormat::HailoYoloNMS;
    }

    // Unknown: keep behavior predictable.
    return OutputFormat::UltralyticsYolo;
}

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

static inline bool isFinitePositive(float v) {
    return std::isfinite(v) && (v > 0.0f);
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

static inline CoordOrder coordOrderCode(const pek::AttributeMap &attrs) {
    const std::string order = attrs.getStringOrDefault("coordOrder", "yxyx");

    if (order == "xyxy")
        return CoordOrder::xyxy;

    if (order == "yxyx")
        return CoordOrder::yxyx;

    if (order == "xywh")
        return CoordOrder::yxyx;

    return CoordOrder::xywh;
}

static void fillDetection(const std::vector<Det> &dets,
                          const pek::TensorParser::Input &input,
                          pek::Perception::Layer &detectionResult,
                          bool normalizeOutputCoordinates) {
    const float frameWidth = static_cast<float>(input.inferenceInfo.image.width);
    const float frameHeight = static_cast<float>(input.inferenceInfo.image.height);

    for (const auto &a : dets) {
        Perception::Rect rect;
        rect.x = a.x1;
        rect.y = a.y1;
        rect.width = a.x2 - a.x1;
        rect.height = a.y2 - a.y1;
        rect.confidence = a.conf;
        rect.classId = a.cls;
        rect.text = pek::resources::Labels::getLabel(pek::resources::LabelType::Coco, a.cls);

        if (normalizeOutputCoordinates) {
            rect.x /= frameWidth;
            rect.width /= frameWidth;
            rect.y /= frameHeight;
            rect.height /= frameHeight;
        }

        detectionResult.detections.push_back(rect);
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

// ----------------------------------------------------------------------------

Result<void> YoloParser::parse(const pek::TensorParser::Input &input,
                               pek::Perception::Layer &detectionResult) {

    const OutputFormat outputFormat = parseOutputFormat(input.attributes);

    const float confThreshold =
        (float)input.attributes.getDoubleOrDefault("confidenceThreshold", 0.25);
    const float iouThreshold = (float)input.attributes.getDoubleOrDefault("iouThreshold", 0.45);
    const bool normalizeOutputCoordinates =
        (float)input.attributes.getBoolOrDefault("normalizeOutputCoordinates", true);
    const bool applyNms = (float)input.attributes.getBoolOrDefault("applyNms", true);

    const bool debug = input.attributes.getBoolOrDefault("debug", false);

    const int classCount = static_cast<int>(input.attributes.getIntOrDefault(
        "classCount", input.attributes.getIntOrDefault("classes", 80)));
    const int maxBboxesPerClass = static_cast<int>(input.attributes.getIntOrDefault(
        "maxBboxesPerClass", input.attributes.getIntOrDefault("max_bboxes_per_class", 100)));
    const int64_t maxDetections = input.attributes.getIntOrDefault("maxDetections", 5);
    const CoordOrder coordOrder = coordOrderCode(input.attributes);

    const auto &image = input.inferenceInfo.image;
    const size_t frameWidth = image.width;
    const size_t frameHeight = image.height;

    const size_t modelWidth = image.modelWidth;
    const size_t modelHeight = image.modelHeight;

    const TensorView &tensor = *input.tensors[0];
    const pek::Shape shape = input.tensors[0]->getShape();

    assert(frameWidth != 0);
    assert(frameHeight != 0);
    assert(modelWidth != 0);
    assert(modelHeight != 0);
    assert(input.tensors[0]);
    assert(input.inferenceInfo.image.modelWidth == input.inferenceInfo.image.modelHeight);

    std::vector<Det> dets;

    if (outputFormat == OutputFormat::HailoYoloNMS) {
        assert(classCount > 0);
        assert(maxBboxesPerClass > 0);

        // Accept packed tensor shape [1, classCount, flat]
        if (shape.rank != 3 || shape.dims[0] != 1 || shape.dims[1] != classCount) {
            return tl::unexpected(PEK_ERROR(
                pek::ErrorFlag::InvalidData,
                fmt::format("YoloParser: expected packed tensor shape [1,classCount,flat], got {}",
                            shape.toString())));
        }

        dets.reserve(static_cast<size_t>(classCount) * static_cast<size_t>(maxBboxesPerClass));

        // Packed format: [num_det, det(5)*num_det, num_det, ...] per class
        // This means not the whole tensor will be filled but only the necessery ammount of
        // detection.
        size_t offset = 0;
        for (int classId = 0; (classId < classCount) && (offset < tensor.getCount()); ++classId) {
            // With the packed detection tensor the first value is the number of detections per
            // class.
            int numDet = static_cast<int>(tensor.get(offset));
            numDet = (numDet < 0) ? 0 : (std::min(numDet, maxBboxesPerClass));
            offset += 1;

            for (int detIdx = 0; detIdx < numDet && (offset + 4) < tensor.getCount(); ++detIdx) {
                // The number of detections are followed by xyxy or yxyx and a score.
                float a0 = tensor.get(offset + 0);
                float a1 = tensor.get(offset + 1);
                float a2 = tensor.get(offset + 2);
                float a3 = tensor.get(offset + 3);
                float score = tensor.get(offset + 4);
                offset += 5;

                if (!std::isfinite(score) || score <= 0.0f || score < confThreshold)
                    continue;

                float y1, x1, y2, x2;
                if (coordOrder == CoordOrder::yxyx) {
                    y1 = a0;
                    x1 = a1;
                    y2 = a2;
                    x2 = a3;
                } else {
                    x1 = a0;
                    y1 = a1;
                    x2 = a2;
                    y2 = a3;
                }

                if (!(std::isfinite(x1) && std::isfinite(y1) && std::isfinite(x2) &&
                      std::isfinite(y2)))
                    continue;
                if (!isFinitePositive(std::fabs(x2 - x1)) || !isFinitePositive(std::fabs(y2 - y1)))
                    continue;

                Det d{x1, y1, x2, y2, score, classId};

                processDetection(input, d, image);

                if (d.x2 <= d.x1 || d.y2 <= d.y1)
                    continue;

                dets.push_back(d);
            }
        }

        size_t numResults = std::min<size_t>(static_cast<size_t>(maxDetections), dets.size());
        std::partial_sort(dets.begin(),
                          dets.begin() + numResults,
                          dets.end(),
                          [](const Det &a, const Det &b) { return a.conf > b.conf; });

        dets.resize(numResults);
    } else if (outputFormat == OutputFormat::UltralyticsYolo) {
        // Assume tensor is [*, C, N] or [*, N, C] and the smaller one is C
        bool colFirst = true;
        size_t C = shape.dims[1];
        size_t N = shape.dims[2];

        if (C > N) {
            C = shape.dims[2];
            N = shape.dims[1];
            colFirst = false;
        }

        dets.reserve(N);

        // iterate over candidates
        // Set up indexing for this candidate:
        // - colFirst: [C x N] row-major, index = row * N + col
        //             candidate index = column i
        // - !colFirst: [N x C] row-major, index = row * C + col
        //              candidate index = row i
        for (size_t i = 0; i < N; ++i) {

            size_t base;
            int64_t stride;

            if (colFirst) {
                base = i;            // col = i
                stride = (int64_t)N; // next channel is +N
            } else {
                base = i * C; // row = i
                stride = 1;   // channels contiguous
            }

            auto get_ch = [&](size_t ch) -> float { return tensor.get(base + ch * stride); };

            // read box (cx,cy,w,h) from first 4 channels
            const float cx = get_ch(0);
            const float cy = get_ch(1);
            const float bw = get_ch(2);
            const float bh = get_ch(3);

            // find best class over channels [4 .. C-1]
            int best = -1;
            float bestp = 0.0f;

            for (size_t c = 0; c < C - 4; ++c) {
                const float sc = get_ch(4 + c);
                if (sc > bestp) {
                    bestp = sc;
                    best = static_cast<int>(c);
                }
            }

            if (bestp < confThreshold)
                continue;

            // convert to xyxy in model space
            const float x1 = cx - bw * 0.5f;
            const float y1 = cy - bh * 0.5f;
            const float x2 = cx + bw * 0.5f;
            const float y2 = cy + bh * 0.5f;

            // scale to frame space
            Det d{x1, y1, x2, y2, bestp, best};

            processDetection(input, d, image);

            dets.push_back(d);
        }
    }

    if (dets.size() > 0) {
        if (applyNms)
            nms(dets, iouThreshold);
        fillDetection(dets, input, detectionResult, normalizeOutputCoordinates);
    }
    detectionResult.contentType = "genericObject";

    return {};
}
