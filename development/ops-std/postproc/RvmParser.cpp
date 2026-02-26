/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "postproc/RvmParser.h"
#include "amp/Perception.h"
#include "amp/TensorParser.h"
#include "amp/Types.h"

#include <cmath>
#include <cstdint>
#include <fmt/core.h>
#include <string>

using namespace amp;

amp::Result<void> RvmParser::parse(const amp::TensorParser::Input &input,
                                   amp::Perception::Layer &detectionResult) {

    assert(input.tensors[0]);
    assert(input.tensors[1]);

    assert(input.tensors[1]->getShape().dimensionCount == 4);
    assert(input.tensors[1]->getShape().valueCount[1] == 1);
    assert(input.tensors[1]->getShape().valueCount[1] == 1);

    size_t maskHeight = input.tensors[1]->getShape().valueCount[2];
    size_t maskWidth = input.tensors[1]->getShape().valueCount[3];

    detectionResult.detections.push_back(Perception::SegmentationMap());

    auto &sm = std::get<Perception::SegmentationMap>(detectionResult.detections.back());
    sm.bitmap = amp::Bitmap(amp::Bitmap::Type::Uint8, maskWidth, maskHeight);

    uint8_t *dst = (uint8_t *)sm.bitmap.getData();

    int planeSize = maskHeight * maskWidth;

    for (size_t y = 0; y < maskHeight; y++) {
        for (size_t x = 0; x < maskWidth; x++) {

            float r = input.tensors[0]->get(0 * planeSize + y * maskWidth + x);
            float g = input.tensors[0]->get(1 * planeSize + y * maskWidth + x);
            float b = input.tensors[0]->get(2 * planeSize + y * maskWidth + x);
            float a = input.tensors[1]->get(y * maskWidth + x);

            dst[y * maskWidth + x] = (a > 0.5f) ? 0 : 255;
        }
    }

    detectionResult.contentType = "segmentationReplaceLayer";

    return {};
}

/*amp::Result<void> RvmParser::parse(const amp::TensorParser::Input &input,
                                  amp::Perception::Layer &detectionResult) {

    assert(input.tensors[0]);
    assert(input.tensors[1]);

    assert(input.tensors[1]->getShape().dimensionCount == 4);
    assert(input.tensors[1]->getShape().valueCount[1] == 1);

    const size_t H = input.tensors[1]->getShape().valueCount[2];
    const size_t W = input.tensors[1]->getShape().valueCount[3];
    const size_t N = W * H;

    detectionResult.detections.push_back(Perception::SegmentationMap());
    auto &sm = std::get<Perception::SegmentationMap>(detectionResult.detections.back());
    sm.bitmap = amp::Bitmap(amp::Bitmap::Type::Uint8, W, H);

    uint8_t *dst = const_cast<uint8_t *>(sm.bitmap.getData());

    // ---- Tunables ----
    const int radius = 2;                // 1..3 typical, 2 = 5x5 neighborhood
    const uint8_t thresh = 128;          // 0..255; 128 corresponds to 0.5
    // ------------------

    // Store original alpha (0..255) in orig
    std::vector<uint8_t> orig(N);
    for (size_t i = 0; i < N; ++i) {
        float a = input.tensors[1]->get(i);
        if (a < 0.0f) a = 0.0f;
        if (a > 1.0f) a = 1.0f;
        orig[i] = static_cast<uint8_t>(a * 255.0f + 0.5f);
    }

    // Grayscale dilation (max filter) into tmp
    std::vector<uint8_t> tmp(N);
    auto dilate_max = [&](const uint8_t* src, uint8_t* out, int r) {
        const int Wi = static_cast<int>(W);
        const int Hi = static_cast<int>(H);
        for (int y = 0; y < Hi; ++y) {
            const int y0 = std::max(0, y - r);
            const int y1 = std::min(Hi - 1, y + r);
            for (int x = 0; x < Wi; ++x) {
                const int x0 = std::max(0, x - r);
                const int x1 = std::min(Wi - 1, x + r);

                uint8_t m = 0;
                for (int yy = y0; yy <= y1; ++yy) {
                    const uint8_t* row = src + yy * Wi;
                    for (int xx = x0; xx <= x1; ++xx) {
                        m = std::max(m, row[xx]);
                    }
                }
                out[y * Wi + x] = m;
            }
        }
    };

    // Grayscale erosion (min filter) into out
    auto erode_min = [&](const uint8_t* src, uint8_t* out, int r) {
        const int Wi = static_cast<int>(W);
        const int Hi = static_cast<int>(H);
        for (int y = 0; y < Hi; ++y) {
            const int y0 = std::max(0, y - r);
            const int y1 = std::min(Hi - 1, y + r);
            for (int x = 0; x < Wi; ++x) {
                const int x0 = std::max(0, x - r);
                const int x1 = std::min(Wi - 1, x + r);

                uint8_t m = 255;
                for (int yy = y0; yy <= y1; ++yy) {
                    const uint8_t* row = src + yy * Wi;
                    for (int xx = x0; xx <= x1; ++xx) {
                        m = std::min(m, row[xx]);
                    }
                }
                out[y * Wi + x] = m;
            }
        }
    };

    // Closing: dilate then erode
    // closed = erode_min(dilate_max(orig))
    std::vector<uint8_t> closed(N);
    dilate_max(orig.data(), tmp.data(), radius);
    erode_min(tmp.data(), closed.data(), radius);

    // Fill holes only: alphaFilled = max(orig, closed)
    // Then threshold into your binary segmentation bitmap:
    // foreground (subject) = 0, background = 255
    for (size_t i = 0; i < N; ++i) {
        const uint8_t aFilled = std::max(orig[i], closed[i]);
        dst[i] = (aFilled >= thresh) ? 0 : 255;
    }

    detectionResult.contentType = "ocrDetectionSegmentation";
    return {};
}
*/
