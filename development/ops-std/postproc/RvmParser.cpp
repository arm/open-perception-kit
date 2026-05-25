/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "postproc/RvmParser.h"
#include "pek/Perception.h"
#include "pek/TensorParser.h"
#include "pek/Types.h"

#include <cmath>
#include <cstdint>
#include <fmt/core.h>
#include <string>

using namespace pek;

pek::Result<void>
stdop::postproc::parser::RvmParser::parse(const pek::TensorParser::Input &input,
                                          pek::Perception::Layer &detectionResult) {

    if (!input.tensors[0] || !input.tensors[1]) {
        return tl::unexpected(
            PEK_ERROR(ErrorFlag::InvalidData, "RvmParser: input tensors are null"));
    }

    const auto shape = input.tensors[1]->getShape();
    if (shape.rank != 4) {
        return tl::unexpected(
            PEK_ERROR(ErrorFlag::InvalidData,
                      fmt::format("RvmParser: expected 4D tensor, got {}D", shape.rank)));
    }
    if (shape.dims[0] != 1) {
        return tl::unexpected(
            PEK_ERROR(ErrorFlag::InvalidData,
                      fmt::format("RvmParser: batch size must be 1, got {}", shape.dims[0])));
    }
    if (shape.dims[1] != 1) {
        return tl::unexpected(
            PEK_ERROR(ErrorFlag::InvalidData,
                      fmt::format("RvmParser: expected 1 channel, got {}", shape.dims[1])));
    }

    size_t maskHeight = shape.dims[2];
    size_t maskWidth = shape.dims[3];

    detectionResult.detections.push_back(Perception::SegmentationMap());

    auto &sm = std::get<Perception::SegmentationMap>(detectionResult.detections.back());
    sm.bitmap = pek::Bitmap(pek::Bitmap::Type::Uint8, maskWidth, maskHeight);

    uint8_t *dst = (uint8_t *)sm.bitmap.getData();

    // size_t planeSize = maskHeight * maskWidth;

    for (size_t y = 0; y < maskHeight; y++) {
        for (size_t x = 0; x < maskWidth; x++) {

            /*float r = input.tensors[0]->get(0 * planeSize + y * maskWidth + x);
            float g = input.tensors[0]->get(1 * planeSize + y * maskWidth + x);
            float b = input.tensors[0]->get(2 * planeSize + y * maskWidth + x);*/
            float a = input.tensors[1]->get(y * maskWidth + x);

            dst[y * maskWidth + x] = (a > 0.5f) ? 0 : 255;
        }
    }

    detectionResult.contentType = "segmentation";
    detectionResult.compositingMode = "backgroundReplacement";

    return {};
}
