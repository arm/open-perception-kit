/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#include "postproc/UltrafaceParser.h"
#include "pek/Perception.h"

#include <algorithm>
#include <cmath>
#include <fmt/core.h>
#include <vector>

using namespace pek;

// IoU between two boxes (x,y = top-left, w,h = size)
inline float iou(const Perception::Rect &a, const Perception::Rect &b) {
    float ax2 = a.x + a.width;
    float ay2 = a.y + a.height;
    float bx2 = b.x + b.width;
    float by2 = b.y + b.height;

    float interLeft = std::max(a.x, b.x);
    float interTop = std::max(a.y, b.y);
    float interRight = std::min(ax2, bx2);
    float interBottom = std::min(ay2, by2);

    float interW = interRight - interLeft;
    float interH = interBottom - interTop;

    if (interW <= 0.0f || interH <= 0.0f)
        return 0.0f;

    float interArea = interW * interH;
    float areaA = a.width * a.height;
    float areaB = b.width * b.height;

    float unionArea = areaA + areaB - interArea;
    if (unionArea <= 0.0f)
        return 0.0f;

    return interArea / unionArea;
}

// Greedy NMS:
//  - detections: all raw boxes (no pre-thresholding)
//  - scoreThreshold: drop boxes with confidence < scoreThreshold
//  - iouThreshold: IoU >= this → suppress lower-confidence box
inline std::vector<Perception::Detection>
nonMaxSuppression(const std::vector<Perception::Detection> &detections,
                  float scoreThreshold,
                  float iouThreshold) {
    // 1) Filter by score
    std::vector<Perception::Detection> candidates;
    candidates.reserve(detections.size());
    for (const auto &det : detections) {
        const auto &d = std::get<pek::Perception::Rect>(det);

        if (d.confidence >= scoreThreshold)
            candidates.push_back(d);
    }

    if (candidates.empty())
        return {};

    // 2) Sort by confidence descending
    std::sort(candidates.begin(),
              candidates.end(),
              [](const Perception::Detection &da, const Perception::Detection &db) {
                  const auto &a = std::get<pek::Perception::Rect>(da);
                  const auto &b = std::get<pek::Perception::Rect>(db);
                  return a.confidence > b.confidence;
              });

    // 3) Greedy NMS
    std::vector<Perception::Detection> result;
    std::vector<bool> suppressed(candidates.size(), false);

    for (size_t i = 0; i < candidates.size(); ++i) {
        if (suppressed[i])
            continue;

        const Perception::Rect &current = std::get<Perception::Rect>(candidates[i]);
        result.push_back(current);

        for (size_t j = i + 1; j < candidates.size(); ++j) {
            if (suppressed[j])
                continue;

            if (iou(current, std::get<Perception::Rect>(candidates[j])) >= iouThreshold) {
                suppressed[j] = true;
            }
        }
    }

    return result;
}

// ----------------------------------------------------------------------------

struct Anchor {
    float cx, cy, w, h;
};

struct ValidatedUltraFaceInput {
    const TensorView *scores = nullptr;
    const TensorView *boxes = nullptr;
    size_t detectionCount = 0;
};

static std::vector<Anchor> generateAnchors(size_t image_w, size_t image_h) {
    assert(image_w == 320);
    assert(image_h == 240);

    // Feature map sizes (often computed with ceil(image/stride))
    const int feature_map_w[4] = {40, 20, 10, 5};
    const int feature_map_h[4] = {30, 15, 8, 4};

    // Strides (shrinkage) per feature level (UltraFace typical)
    const int shrinkage_w[4] = {8, 16, 32, 64};
    const int shrinkage_h[4] = {8, 16, 32, 64};

    // min_boxes per feature level (same order as Python)
    const int min_boxes[4][3] = {
        {10, 16, 24},   // level 0  (3 anchors)
        {32, 48, -1},   // level 1  (2 anchors, ignore -1)
        {64, 96, -1},   // level 2  (2 anchors, ignore -1)
        {128, 192, 256} // level 3  (3 anchors)
    };

    std::vector<Anchor> priors;
    priors.reserve(4420); // known count for 320x240

    for (int k = 0; k < 4; ++k) {
        const int fm_w = feature_map_w[k];
        const int fm_h = feature_map_h[k];

        // IMPORTANT:
        // Use image/stride as the normalization scale (not fm_w/fm_h),
        // because fm sizes are often ceil(image/stride) and using fm_* directly
        // can bias anchor centers (often showing up as slight down/right shifts).
        const float scale_w = static_cast<float>(image_w) / static_cast<float>(shrinkage_w[k]);
        const float scale_h = static_cast<float>(image_h) / static_cast<float>(shrinkage_h[k]);

        for (int j = 0; j < fm_h; ++j) {     // over height
            for (int i = 0; i < fm_w; ++i) { // over width
                const float cx = (static_cast<float>(i) + 0.5f) / scale_w;
                const float cy = (static_cast<float>(j) + 0.5f) / scale_h;

                // add anchors of different sizes at this location
                for (int mb_idx = 0; mb_idx < 3; ++mb_idx) {
                    const int box = min_boxes[k][mb_idx];
                    if (box <= 0)
                        continue; // skip unused slots (-1)

                    const float w = static_cast<float>(box) / static_cast<float>(image_w);
                    const float h = static_cast<float>(box) / static_cast<float>(image_h);

                    priors.push_back(Anchor{cx, cy, w, h});
                }
            }
        }
    }

    // Optional: clamp to [0,1] like the original code
    auto clamp01 = [](float v) {
        if (v < 0.0f)
            return 0.0f;
        if (v > 1.0f)
            return 1.0f;
        return v;
    };
    for (auto &a : priors) {
        a.cx = clamp01(a.cx);
        a.cy = clamp01(a.cy);
        a.w = clamp01(a.w);
        a.h = clamp01(a.h);
    }

    // priors.size() should be 4420 here for 320x240
    return priors;
}

static std::vector<Anchor> anchors;

static pek::Result<ValidatedUltraFaceInput>
validateParseInput(const pek::TensorParser::Input &input) {
    const size_t modelWidth = input.inferenceInfo.image.modelWidth;
    const size_t modelHeight = input.inferenceInfo.image.modelHeight;
    if (modelWidth == 0 || modelHeight == 0) {
        return tl::unexpected(
            PEK_ERROR(ErrorFlag::InvalidData,
                      fmt::format("UltraFaceParser: model dimensions must be > 0, got {}x{}",
                                  modelWidth,
                                  modelHeight)));
    }

    if (modelWidth != 320 || modelHeight != 240) {
        return tl::unexpected(PEK_ERROR(
            ErrorFlag::InvalidData,
            fmt::format("UltraFaceParser: unsupported model dimensions {}x{}, expected 320x240",
                        modelWidth,
                        modelHeight)));
    }

    const TensorView *scores = input.tensors[0];
    const TensorView *boxes = input.tensors[1];
    if (!scores || !boxes) {
        return tl::unexpected(PEK_ERROR(ErrorFlag::InvalidData,
                                        "UltraFaceParser: score and box tensors are required"));
    }

    const auto scoresShape = scores->getShape();
    const auto boxesShape = boxes->getShape();
    if (scoresShape.dimensionCount != 3) {
        return tl::unexpected(
            PEK_ERROR(ErrorFlag::InvalidData,
                      fmt::format("UltraFaceParser: scores tensor must be 3D, got {}D",
                                  scoresShape.dimensionCount)));
    }
    if (boxesShape.dimensionCount != 3) {
        return tl::unexpected(
            PEK_ERROR(ErrorFlag::InvalidData,
                      fmt::format("UltraFaceParser: boxes tensor must be 3D, got {}D",
                                  boxesShape.dimensionCount)));
    }
    if (scoresShape.valueCount[0] != 1) {
        return tl::unexpected(
            PEK_ERROR(ErrorFlag::InvalidData,
                      fmt::format("UltraFaceParser: scores batch size must be 1, got {}",
                                  scoresShape.valueCount[0])));
    }
    if (boxesShape.valueCount[0] != 1) {
        return tl::unexpected(
            PEK_ERROR(ErrorFlag::InvalidData,
                      fmt::format("UltraFaceParser: boxes batch size must be 1, got {}",
                                  boxesShape.valueCount[0])));
    }
    if (scoresShape.valueCount[2] != 2) {
        return tl::unexpected(
            PEK_ERROR(ErrorFlag::InvalidData,
                      fmt::format("UltraFaceParser: scores tensor last dimension must be 2, got {}",
                                  scoresShape.valueCount[2])));
    }
    if (boxesShape.valueCount[2] != 4) {
        return tl::unexpected(
            PEK_ERROR(ErrorFlag::InvalidData,
                      fmt::format("UltraFaceParser: boxes tensor last dimension must be 4, got {}",
                                  boxesShape.valueCount[2])));
    }
    if (scoresShape.valueCount[1] != boxesShape.valueCount[1]) {
        return tl::unexpected(
            PEK_ERROR(ErrorFlag::InvalidData,
                      fmt::format("UltraFaceParser: score and box counts differ: {} vs {}",
                                  scoresShape.valueCount[1],
                                  boxesShape.valueCount[1])));
    }

    if (anchors.empty()) {
        anchors = generateAnchors(modelWidth, modelHeight);
    }

    const size_t detectionCount = boxesShape.valueCount[1];
    if (anchors.size() != detectionCount) {
        return tl::unexpected(PEK_ERROR(
            ErrorFlag::InvalidData,
            fmt::format("UltraFaceParser: anchor count {} does not match detection count {}",
                        anchors.size(),
                        detectionCount)));
    }

    return ValidatedUltraFaceInput{
        .scores = scores, .boxes = boxes, .detectionCount = detectionCount};
}

// ----------------------------------------------------------------------------

pek::Result<void> pek::UltraFaceParser::parse(const pek::TensorParser::Input &input,
                                              pek::Perception::Layer &detectionResult) {

    const float confThreshold =
        (float)input.attributes.getDoubleOrDefault("confidenceThreshold", 0.5);
    const float iouThreshold = (float)input.attributes.getDoubleOrDefault("iouThreshold", 0.3);
    const bool normalizeOutputCoordinates =
        (float)input.attributes.getBoolOrDefault("normalizeOutputCoordinates", true);

    const size_t frameWidth = input.inferenceInfo.image.width;
    const size_t frameHeight = input.inferenceInfo.image.height;
    auto validated = validateParseInput(input);
    if (!validated) {
        return tl::unexpected(validated.error());
    }

    const TensorView *scores = validated->scores;
    const TensorView *boxes = validated->boxes;
    const size_t detectionCount = validated->detectionCount;

    for (size_t i = 0; i < detectionCount; i++) {
        float notFace = scores->get(2 * i + 0);
        float face = scores->get(2 * i + 1);
        float v0 = boxes->get(4 * i + 0);
        float v1 = boxes->get(4 * i + 1);
        float v2 = boxes->get(4 * i + 2);
        float v3 = boxes->get(4 * i + 3);

        float acx = anchors[i].cx;
        float acy = anchors[i].cy;
        float aw = anchors[i].w;
        float ah = anchors[i].h;

        constexpr float CENTER_VAR = 0.1f;
        constexpr float SIZE_VAR = 0.2f;

        // 1) decode center + size in normalized coords
        float pred_cx = acx + v0 * CENTER_VAR * aw;
        float pred_cy = acy + v1 * CENTER_VAR * ah;
        float pred_w = aw * std::exp(v2 * SIZE_VAR);
        float pred_h = ah * std::exp(v3 * SIZE_VAR);

        // 2) to corners
        float x_min = pred_cx - 0.5f * pred_w;
        float y_min = pred_cy - 0.5f * pred_h;
        float x_max = pred_cx + 0.5f * pred_w;
        float y_max = pred_cy + 0.5f * pred_h;

        // 3) (optional but recommended) clamp to [0,1]
        auto clamp01 = [](float v) {
            if (v < 0.0f)
                return 0.0f;
            if (v > 1.0f)
                return 1.0f;
            return v;
        };
        x_min = clamp01(x_min);
        y_min = clamp01(y_min);
        x_max = clamp01(x_max);
        y_max = clamp01(y_max);

        float x1 = x_min;
        float y1 = y_min;
        float x2 = x_max;
        float y2 = y_max;

        // ---- 3) convert to pixel coords ----
        if (false == normalizeOutputCoordinates) {
            x1 *= frameWidth;
            y1 *= frameHeight;
            x2 *= frameWidth;
            y2 *= frameHeight;
        }

        // ---

        Perception::Rect dr;
        dr.x = x1;
        dr.y = y1;
        dr.width = x2 - x1;
        dr.height = y2 - y1;
        dr.confidence = face;
        dr.classId = 0;
        dr.label = "face";
        dr.text = dr.label;

        detectionResult.detections.push_back(dr);
    }

    // detectionResult.rects = nonMaxSuppression(detectionResult.rects, 0.6f, 0.01f);
    detectionResult.detections =
        nonMaxSuppression(detectionResult.detections, confThreshold, iouThreshold);

    detectionResult.contentType = "humanFace";

    return {};
}
