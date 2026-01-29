#include "postproc/UltrafaceParser.h"
#include "amp/PerceptionContext.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace amp;

// IoU between two boxes (x,y = top-left, w,h = size)
inline float iou(const DetectionRect &a, const DetectionRect &b) {
    float ax2 = a.x + a.w;
    float ay2 = a.y + a.h;
    float bx2 = b.x + b.w;
    float by2 = b.y + b.h;

    float interLeft = std::max(a.x, b.x);
    float interTop = std::max(a.y, b.y);
    float interRight = std::min(ax2, bx2);
    float interBottom = std::min(ay2, by2);

    float interW = interRight - interLeft;
    float interH = interBottom - interTop;

    if (interW <= 0.0f || interH <= 0.0f)
        return 0.0f;

    float interArea = interW * interH;
    float areaA = a.w * a.h;
    float areaB = b.w * b.h;

    float unionArea = areaA + areaB - interArea;
    if (unionArea <= 0.0f)
        return 0.0f;

    return interArea / unionArea;
}

// Greedy NMS:
//  - detections: all raw boxes (no pre-thresholding)
//  - scoreThreshold: drop boxes with confidence < scoreThreshold
//  - iouThreshold: IoU >= this → suppress lower-confidence box
inline std::vector<DetectionRect> nonMaxSuppression(const std::vector<DetectionRect> &detections,
                                                    float scoreThreshold,
                                                    float iouThreshold) {
    // 1) Filter by score
    std::vector<DetectionRect> candidates;
    candidates.reserve(detections.size());
    for (const auto &d : detections) {
        if (d.confidence >= scoreThreshold)
            candidates.push_back(d);
    }

    if (candidates.empty())
        return {};

    // 2) Sort by confidence descending
    std::sort(
        candidates.begin(), candidates.end(), [](const DetectionRect &a, const DetectionRect &b) {
            return a.confidence > b.confidence;
        });

    // 3) Greedy NMS
    std::vector<DetectionRect> result;
    std::vector<bool> suppressed(candidates.size(), false);

    for (size_t i = 0; i < candidates.size(); ++i) {
        if (suppressed[i])
            continue;

        const DetectionRect &current = candidates[i];
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

// ----------------------------------------------------------------------------

struct Anchor {
    float cx, cy, w, h;
};

static std::vector<Anchor> generateAnchors(size_t image_w, size_t image_h) {
    assert(image_w == 320);
    assert(image_h == 240);

    // feature map sizes (widths and heights) from fd_config.py
    const int feature_map_w[4] = {40, 20, 10, 5};
    const int feature_map_h[4] = {30, 15, 8, 4};

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

        const float scale_w = static_cast<float>(fm_w); // = image_w / shrinkage_w[k]
        const float scale_h = static_cast<float>(fm_h); // = image_h / shrinkage_h[k]

        for (int j = 0; j < fm_h; ++j) // over height
        {
            for (int i = 0; i < fm_w; ++i) // over width
            {
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
    for (auto &a : priors) {
        auto clamp01 = [](float v) {
            if (v < 0.0f)
                return 0.0f;
            if (v > 1.0f)
                return 1.0f;
            return v;
        };
        a.cx = clamp01(a.cx);
        a.cy = clamp01(a.cy);
        a.w = clamp01(a.w);
        a.h = clamp01(a.h);
    }

    // priors.size() should be 4420 here for 320x240
    return priors;
}

static std::vector<Anchor> anchors;

// ----------------------------------------------------------------------------

amp::Result<void> amp::UltraFaceParser::parse(const amp::TensorParser::Input &input,
                                              amp::RawDetectionLayer &detectionResult) {

    const float confThreshold = (input.parserSettings.confidenceThreshold == 0.0f)
                                    ? 0.5f
                                    : input.parserSettings.confidenceThreshold;

    const float iouThreshold =
        (input.parserSettings.iouThreshold == 0.0f) ? 0.3f : input.parserSettings.iouThreshold;

    const size_t frameWidth = input.inferenceInfo.image.width;
    const size_t frameHeight = input.inferenceInfo.image.height;
    if (anchors.empty())
        anchors = generateAnchors(input.inferenceInfo.image.modelWidth,
                                  input.inferenceInfo.image.modelHeight);

    const TensorView *scores = input.tensors[0];
    const TensorView *boxes = input.tensors[1];

    assert(input.inferenceInfo.image.modelWidth > 0);
    assert(input.inferenceInfo.image.modelHeight > 0);
    assert(scores != nullptr);
    assert(boxes != nullptr);
    assert(scores != nullptr);
    assert(boxes->getShape().dimensionCount == 3);
    assert(scores->getShape().dimensionCount == 3);
    assert(boxes->getShape().valueCount[0] == 1);
    assert(scores->getShape().valueCount[0] == 1);
    assert(boxes->getShape().valueCount[2] == 4);
    assert(scores->getShape().valueCount[2] == 2);

    size_t detectionCount = boxes->getShape().valueCount[1];

    assert(anchors.size() == detectionCount);

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

        /*
        // ---- 1) decode center + size (normalized) ----
        float pred_cx = v0 * aw + acx;
        float pred_cy = v1 * ah + acy;
        float pred_w  = std::exp(v2) * aw;
        float pred_h  = std::exp(v3) * ah;

        // ---- 2) convert to normalized corner coords ----
        float x_min = pred_cx - pred_w * 0.5f;
        float y_min = pred_cy - pred_h * 0.5f;
        float x_max = pred_cx + pred_w * 0.5f;
        float y_max = pred_cy + pred_h * 0.5f;
        */
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
        if (false == input.parserSettings.normalizedCoordinates) {
            x1 *= frameWidth;
            y1 *= frameHeight;
            x2 *= frameWidth;
            y2 *= frameHeight;
        }

        // ---

        DetectionRect dr;
        dr.x = x1;
        dr.y = y1;
        dr.w = x2 - x1;
        dr.h = y2 - y1;
        dr.confidence = face;
        dr.label = "";

        detectionResult.rects.push_back(dr);
    }

    // detectionResult.rects = nonMaxSuppression(detectionResult.rects,
    // settings.confidenceThreshold, settings.iouThreshold);
    detectionResult.rects = nonMaxSuppression(detectionResult.rects, 0.6f, 0.01f);

    return {};
}
