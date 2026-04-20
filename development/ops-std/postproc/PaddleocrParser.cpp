/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "postproc/PaddleocrParser.h"
#include "amp/Bitmap.h"
#include "amp/Perception.h"

#include <cmath>
#include <cstdint>

using namespace amp;

amp::Result<void> PaddleOcrDetectionParser::parse(const amp::TensorParser::Input &input,
                                                  amp::Perception::Layer &detectionResult) {

    const float thresholdLow = (float)input.attributes.getDoubleOrDefault("thresholdLow", 0.60f);
    const float thresholdHigh = (float)input.attributes.getDoubleOrDefault("thresholdHigh", 0.80f);
    const float gamma = (float)input.attributes.getDoubleOrDefault("gamma", 0.5f);

    assert(input.tensors[0]);
    assert(input.tensors[0]->getShape().dimensionCount == 4);
    assert(input.tensors[0]->getShape().valueCount[1] == 1);

    const size_t maskHeight = input.tensors[0]->getShape().valueCount[2];
    const size_t maskWidth = input.tensors[0]->getShape().valueCount[3];

    detectionResult.detections.push_back(Perception::SegmentationMap());
    auto &sm = std::get<Perception::SegmentationMap>(detectionResult.detections.back());
    sm.bitmap = amp::Bitmap(amp::Bitmap::Type::Uint8, maskWidth, maskHeight);

    uint8_t *dst = const_cast<uint8_t *>(sm.bitmap.getData());

    auto smoothstep = [](float e0, float e1, float x) {
        x = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    };

    const size_t pixelCount = maskWidth * maskHeight;

    for (size_t i = 0; i < pixelCount; ++i) {

        float logit = input.tensors[0]->get(i);

        // Sigmoid → probability
        float p = 1.0f / (1.0f + std::exp(-logit));

        if (p <= thresholdLow) {
            dst[i] = 0;
            continue;
        }

        p = std::clamp(p, 0.0f, 1.0f);

        // Soft threshold
        float a = smoothstep(thresholdLow, thresholdHigh, p);

        // Optional contrast shaping
        a = std::pow(a, gamma);

        dst[i] = static_cast<uint8_t>(std::lround(a * 255.0f));
    }

    detectionResult.contentType = "segmentation";
    detectionResult.compositingMode = "overlay";
    return {};
}
