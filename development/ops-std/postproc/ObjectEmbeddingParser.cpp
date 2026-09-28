/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "postproc/ObjectEmbeddingParser.h"

#include <cmath>
#include <fmt/core.h>
#include <memory>
#include <utility>

using namespace opk;
using namespace opk::stdop::postproc;

Result<void> ObjectEmbeddingParser::parse(const TensorParser::Input &input,
                                          open_perception_kit::FrameResults &results) {

    if (!input.tensors[0]) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData,
                      fmt::format("ObjectEmbeddingParser: missing required input tensor")));
    }

    const auto shape = input.tensors[0]->getShape();
    if (shape.rank != 2 || shape.dims[0] != 1) {
        return tl::unexpected(OPK_ERROR(
            ErrorFlag::InvalidData,
            fmt::format("ObjectEmbeddingParser expects [1,N], got {}", shape.toString())));
    }

    const size_t embeddingSize = shape.dims[1];

    auto embedding = std::make_unique<open_perception_kit::metadata::ObjectEmbeddingT>();
    embedding->object = open_perception_kit::makeObjectMeta(0U, input.inferenceInfo.parentId);
    embedding->values.resize(embeddingSize);

    float l2Norm = 0.0f;
    for (size_t i = 0; i < embeddingSize; ++i) {
        const float value = input.tensors[0]->get(i);
        embedding->values[i] = value;
        l2Norm += value * value;
    }

    l2Norm = std::sqrt(l2Norm);
    if (l2Norm > 1e-12f) {
        const float invNorm = 1.0f / l2Norm;
        for (auto &v : embedding->values) {
            v *= invNorm;
        }
    }

    open_perception_kit::metadata::ObjectEmbeddingsT payload;
    payload.layer =
        open_perception_kit::makeLayerInfo({.model = input.inferenceInfo.modelName,
                                            .inferElementId = input.inferenceInfo.inferElementId,
                                            .contentType = k_content_type,
                                            .producer = &input.producerInfo});
    payload.embeddings.push_back(std::move(embedding));
    results.add(std::move(payload));

    return {};
}
