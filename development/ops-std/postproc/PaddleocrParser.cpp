/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

#include "postproc/PaddleocrParser.h"
#include "opk/Bitmap.h"

#include <cmath>
#include <cstdint>
#include <fmt/core.h>
#include <memory>
#include <utility>

using namespace opk;
using namespace opk::stdop::postproc;

opk::Result<void> PaddleOcrDetectionParser::parse(const opk::TensorParser::Input &input,
                                                  open_perception_kit::FrameResults &results) {

    const float thresholdLow = (float)input.attributes.getDoubleOrDefault("thresholdLow", 0.60f);
    const float thresholdHigh = (float)input.attributes.getDoubleOrDefault("thresholdHigh", 0.80f);
    const float gamma = (float)input.attributes.getDoubleOrDefault("gamma", 0.5f);

    if (!input.tensors[0]) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData, "PaddleOcrDetectionParser: input tensor is null"));
    }

    const auto shape = input.tensors[0]->getShape();
    if (shape.rank != 4) {
        return tl::unexpected(OPK_ERROR(
            ErrorFlag::InvalidData,
            fmt::format("PaddleOcrDetectionParser: expected 4D tensor, got {}D", shape.rank)));
    }
    if (shape.dims[1] != 1) {
        return tl::unexpected(OPK_ERROR(
            ErrorFlag::InvalidData,
            fmt::format("PaddleOcrDetectionParser: expected 1 channel, got {}", shape.dims[1])));
    }

    const size_t maskHeight = shape.dims[2];
    const size_t maskWidth = shape.dims[3];

    opk::Bitmap bitmap(opk::Bitmap::Type::Uint8, maskWidth, maskHeight);

    auto *dst = bitmap.getMutableData();

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

    auto mask = std::make_unique<open_perception_kit::metadata::SegmentationMaskT>();
    mask->object = open_perception_kit::makeObjectMeta(0U, input.inferenceInfo.parentId);
    mask->bitmap = open_perception_kit::makeBitmapData(bitmap);

    open_perception_kit::metadata::SegmentationMasksT payload;
    payload.layer =
        open_perception_kit::makeLayerInfo({.model = input.inferenceInfo.modelName,
                                            .inferElementId = input.inferenceInfo.inferElementId,
                                            .contentType = k_content_type,
                                            .compositingMode = "overlay",
                                            .producer = &input.producerInfo});
    payload.masks.push_back(std::move(mask));
    results.add(std::move(payload));
    return {};
}
