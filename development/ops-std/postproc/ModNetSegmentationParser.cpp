/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "ModNetSegmentationParser.h"
#include <algorithm>

namespace pek {

Result<void> stdop::postproc::parser::ModNetSegmentationParser::parse(const Input &input,
                                                                      Perception::Layer &layer) {
    layer.contentType = "segmentation";
    layer.compositingMode = "backgroundReplacement";

    const float thresholdLow = (float)input.attributes.getDoubleOrDefault("thresholdLow", 0.2f);
    const float thresholdHigh = (float)input.attributes.getDoubleOrDefault("thresholdHigh", 0.8f);

    // MODNet outputs a single tensor: alpha matte [1, 1, H, W]
    const auto *outputTensor = input.tensors[0];
    if (!outputTensor) {
        return tl::unexpected(PEK_ERROR(ErrorFlag::InvalidData, "No output tensor"));
    }

    const auto shape = outputTensor->getShape();

    // Validate shape: [1, 1, height, width]
    if (shape.rank != 4 || shape.dims[0] != 1 || shape.dims[1] != 1) {
        return tl::unexpected(
            PEK_ERROR(ErrorFlag::InvalidData, "Invalid shape: expected [1,1,H,W]"));
    }

    size_t height = shape.dims[2];
    size_t width = shape.dims[3];

    // Create bitmap for the alpha matte
    Bitmap alphaMatte(Bitmap::Type::Uint8, width, height);

    // Convert float [0.0, 1.0] to uint8 [0, 255]
    size_t idx = 0;
    for (size_t y = 0U; y < height; ++y) {
        for (size_t x = 0U; x < width; ++x) {
            float alpha = std::clamp(outputTensor->get(idx++), 0.0f, 1.0f);

            if (alpha < thresholdLow) {
                alphaMatte.set8(x, y, 255); // fully background
            } else if (alpha > thresholdHigh) {
                alphaMatte.set8(x, y, 0); // fully foreground
            } else {
                alphaMatte.set8(x, y, static_cast<uint8_t>((1.0f - alpha) * 255.0f));
            }
        }
    }

    // Create SegmentationMap detection
    Perception::SegmentationMap segMap;
    segMap.bitmap = std::move(alphaMatte);

    layer.detections.emplace_back(std::move(segMap));
    return {};
}

} // namespace pek
