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
#include "opk/Embedding.h"

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
    if (shape.rank != 2 || shape.dims[0] != 1 || shape.dims[1] <= 0 ||
        !input.tensors[0]->isValid()) {
        return tl::unexpected(OPK_ERROR(
            ErrorFlag::InvalidData,
            fmt::format("ObjectEmbeddingParser expects [1,N], got {}", shape.toString())));
    }

    const size_t embeddingSize = shape.dims[1];

    auto embedding = std::make_unique<open_perception_kit::metadata::ObjectEmbeddingT>();
    embedding->object = open_perception_kit::makeObjectMeta(0U, input.inferenceInfo.parentId);
    if (input.inferenceInfo.parentId != 0)
        embedding->subject_id = input.inferenceInfo.parentId;
    embedding->space = std::make_unique<open_perception_kit::metadata::EmbeddingSpaceT>();
    embedding->space->id = input.attributes.getStringOrDefault("embeddingSpaceId", "");
    embedding->space->dimensions = static_cast<uint32_t>(embeddingSize);
    embedding->space->metric = open_perception_kit::metadata::EmbeddingMetric::Cosine;
    embedding->values.resize(embeddingSize);

    for (size_t i = 0; i < embeddingSize; ++i) {
        const float value = input.tensors[0]->get(i);
        if (!std::isfinite(value))
            return tl::unexpected(
                OPK_ERROR(ErrorFlag::InvalidData, "ObjectEmbeddingParser: non-finite vector"));
        embedding->values[i] = value;
    }
    if (const auto length = opk::embedding::norm(embedding->values); length) {
        for (auto &v : embedding->values) {
            v = static_cast<float>(v / *length);
        }
    }

    open_perception_kit::metadata::ObjectEmbeddingsT payload;
    payload.layer =
        open_perception_kit::makeLayerInfo({.model = input.inferenceInfo.modelName,
                                            .inferElementId = input.inferenceInfo.inferElementId,
                                            .producer = &input.producerInfo});
    payload.embeddings.push_back(std::move(embedding));
    results.add(std::move(payload));

    return {};
}
