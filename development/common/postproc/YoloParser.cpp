#include "postproc/YoloParser.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace amp;

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

// ----------------------------------------------------------------------------

amp::Result<void> amp::YoloLikeParser::parse(const amp::TensorParser::Input &input,
                                             amp::DetectionResult &detectionResult) {

    const float confThreshold = (input.parserSettings.confidenceThreshold == 0.0f)
                                    ? 0.25f
                                    : input.parserSettings.confidenceThreshold;

    const float iouThreshold =
        (input.parserSettings.iouThreshold == 0.0f) ? 0.45f : input.parserSettings.iouThreshold;

    const size_t frameWidth = input.inferenceInfo.image.width;
    const size_t frameHeight = input.inferenceInfo.image.height;

    assert(input.inferenceInfo.image.modelWidth == input.inferenceInfo.image.modelHeight);

    const size_t yoloModelSquareSize = input.inferenceInfo.image.modelWidth;
    const float sx = static_cast<float>(frameWidth) / yoloModelSquareSize;
    const float sy = static_cast<float>(frameHeight) / yoloModelSquareSize;

    const TensorView &tensor = *input.tensors[0];
    const amp::Shape shape = input.tensors[0]->getShape();

    // Assume tensor is [*, C, N] or [*, N, C] and the smaller one is C
    bool colFirst = true;
    size_t C = shape.valueCount[1];
    size_t N = shape.valueCount[2];

    if (C > N) {
        C = shape.valueCount[2];
        N = shape.valueCount[1];
        colFirst = false;
    }

    std::vector<Det> dets;
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
        Det d{x1 * sx, y1 * sy, x2 * sx, y2 * sy, bestp, best};

        // clamp to frame
        d.x1 = clampf(d.x1, 0, frameWidth - 1);
        d.y1 = clampf(d.y1, 0, frameHeight - 1);
        d.x2 = clampf(d.x2, 0, frameWidth - 1);
        d.y2 = clampf(d.y2, 0, frameHeight - 1);

        dets.push_back(d);
    }

    if (input.parserSettings.applyNms)
        nms(dets, iouThreshold);

    for (const auto &a : dets) {
        DetectionRect rect;
        rect.x = a.x1;
        rect.y = a.y1;
        rect.w = a.x2 - a.x1;
        rect.h = a.y2 - a.y1;
        rect.confidence = a.conf;
        rect.classIndex = a.cls;

        if (input.parserSettings.normalizedCoordinates) {
            rect.x /= input.inferenceInfo.image.width;
            rect.w /= input.inferenceInfo.image.width;
            rect.y /= input.inferenceInfo.image.height;
            rect.h /= input.inferenceInfo.image.height;
        }

        detectionResult.rects.push_back(rect);
    }

    return {};
}
