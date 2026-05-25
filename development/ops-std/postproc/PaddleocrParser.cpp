/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "postproc/PaddleocrParser.h"
#include "pek/Bitmap.h"
#include "pek/Perception.h"

#include <cmath>
#include <cstdint>
#include <fmt/core.h>

using namespace pek;
using namespace pek::stdop::postproc;

Result<void> PaddleOcrDetectionParser::parse(const pek::TensorParser::Input &input,
                                             pek::Perception::Layer &detectionResult) {

    const float thresholdLow = (float)input.attributes.getDoubleOrDefault("thresholdLow", 0.60f);
    const float thresholdHigh = (float)input.attributes.getDoubleOrDefault("thresholdHigh", 0.80f);
    const float gamma = (float)input.attributes.getDoubleOrDefault("gamma", 0.5f);

    if (!input.tensors[0]) {
        return tl::unexpected(
            PEK_ERROR(ErrorFlag::InvalidData, "PaddleOcrDetectionParser: input tensor is null"));
    }

    const auto shape = input.tensors[0]->getShape();
    if (shape.rank != 4) {
        return tl::unexpected(PEK_ERROR(
            ErrorFlag::InvalidData,
            fmt::format("PaddleOcrDetectionParser: expected 4D tensor, got {}D", shape.rank)));
    }
    if (shape.dims[1] != 1) {
        return tl::unexpected(PEK_ERROR(
            ErrorFlag::InvalidData,
            fmt::format("PaddleOcrDetectionParser: expected 1 channel, got {}", shape.dims[1])));
    }

    const size_t maskHeight = shape.dims[2];
    const size_t maskWidth = shape.dims[3];

    detectionResult.detections.push_back(Perception::SegmentationMap());
    auto &sm = std::get<Perception::SegmentationMap>(detectionResult.detections.back());
    sm.bitmap = pek::Bitmap(pek::Bitmap::Type::Uint8, maskWidth, maskHeight);

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
