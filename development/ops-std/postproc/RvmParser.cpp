/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "postproc/RvmParser.h"
#include "opk/TensorParser.h"
#include "opk/Types.h"

#include <cmath>
#include <cstdint>
#include <fmt/core.h>
#include <memory>
#include <string>
#include <utility>

using namespace opk;
using namespace opk::stdop::postproc;

opk::Result<void> RvmParser::parse(const opk::TensorParser::Input &input,
                                   open_perception_kit::FrameResults &results) {

    if (!input.tensors[0] || !input.tensors[1]) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData, "RvmParser: input tensors are null"));
    }

    const auto shape = input.tensors[1]->getShape();
    if (shape.rank != 4) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData,
                      fmt::format("RvmParser: expected 4D tensor, got {}D", shape.rank)));
    }
    if (shape.dims[0] != 1) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData,
                      fmt::format("RvmParser: batch size must be 1, got {}", shape.dims[0])));
    }
    if (shape.dims[1] != 1) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData,
                      fmt::format("RvmParser: expected 1 channel, got {}", shape.dims[1])));
    }

    size_t maskHeight = shape.dims[2];
    size_t maskWidth = shape.dims[3];

    opk::Bitmap bitmap(opk::Bitmap::Type::Uint8, maskWidth, maskHeight);

    auto *dst = bitmap.getMutableData();

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

    auto mask = std::make_unique<open_perception_kit::metadata::SegmentationMaskT>();
    mask->object = open_perception_kit::makeObjectMeta(0U, input.inferenceInfo.parentId);
    mask->bitmap = open_perception_kit::makeBitmapData(bitmap);

    open_perception_kit::metadata::SegmentationMasksT payload;
    payload.layer =
        open_perception_kit::makeLayerInfo({.model = input.inferenceInfo.modelName,
                                            .inferElementId = input.inferenceInfo.inferElementId,
                                            .contentType = k_content_type,
                                            .compositingMode = "backgroundReplacement",
                                            .producer = &input.producerInfo});
    payload.masks.push_back(std::move(mask));
    results.add(std::move(payload));

    return {};
}
