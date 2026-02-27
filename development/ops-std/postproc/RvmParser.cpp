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
    assert(input.tensors[1]->getShape().valueCount[0] == 1);
    assert(input.tensors[1]->getShape().valueCount[1] == 1);

    size_t maskHeight = input.tensors[1]->getShape().valueCount[2];
    size_t maskWidth = input.tensors[1]->getShape().valueCount[3];

    detectionResult.detections.push_back(Perception::SegmentationMap());

    auto &sm = std::get<Perception::SegmentationMap>(detectionResult.detections.back());
    sm.bitmap = amp::Bitmap(amp::Bitmap::Type::Uint8, maskWidth, maskHeight);

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

    return {};
}
