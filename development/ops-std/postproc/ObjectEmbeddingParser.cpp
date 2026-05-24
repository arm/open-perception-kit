/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "postproc/ObjectEmbeddingParser.h"

#include <cmath>
#include <fmt/core.h>

using namespace pek;

Result<void> ObjectEmbeddingParser::parse(const TensorParser::Input &input,
                                          Perception::Layer &detectionResult) {

    if (!input.tensors[0]) {
        return tl::unexpected(
            PEK_ERROR(ErrorFlag::InvalidData,
                      fmt::format("ObjectEmbeddingParser: missing required input tensor")));
    }

    const auto shape = input.tensors[0]->getShape();
    if (shape.rank != 2 || shape.dims[0] != 1) {
        return tl::unexpected(PEK_ERROR(
            ErrorFlag::InvalidData,
            fmt::format("ObjectEmbeddingParser expects [1,N], got {}", shape.toString())));
    }

    const size_t embeddingSize = shape.dims[1];

    Perception::ObjectEmbedding embedding;
    embedding.parentUuid = input.inferenceInfo.parentUuid;
    embedding.values.resize(embeddingSize);

    float l2Norm = 0.0f;
    for (size_t i = 0; i < embeddingSize; ++i) {
        const float value = input.tensors[0]->get(i);
        embedding.values[i] = value;
        l2Norm += value * value;
    }

    l2Norm = std::sqrt(l2Norm);
    if (l2Norm > 1e-12f) {
        const float invNorm = 1.0f / l2Norm;
        for (auto &v : embedding.values) {
            v *= invNorm;
        }
    }

    detectionResult.contentType = "objectEmbedding";
    detectionResult.detections.push_back(std::move(embedding));

    return {};
}
