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

#include "postproc/PersonClassificationParser.h"

#include <cmath>
#include <fmt/core.h>
#include <memory>
#include <utility>

using namespace opk;
using namespace opk::stdop::postproc;

opk::Result<void> PersonClassificationParser::parse(const opk::TensorParser::Input &input,
                                                    open_perception_kit::FrameResults &results) {

    if (!input.tensors[0]) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData, "PersonClassificationParser: input tensor is null"));
    }

    const auto shape = input.tensors[0]->getShape();
    if (shape.rank != 2) {
        return tl::unexpected(OPK_ERROR(
            ErrorFlag::InvalidData,
            fmt::format("PersonClassificationParser: expected 2D tensor, got {}D", shape.rank)));
    }

    if (shape.dims[0] != 1) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData,
                      fmt::format("PersonClassificationParser: batch size must be 1, got {}",
                                  shape.dims[0])));
    }
    if (shape.dims[1] != 2) {
        return tl::unexpected(OPK_ERROR(
            ErrorFlag::InvalidData,
            fmt::format("PersonClassificationParser: expected 2 classes, got {}", shape.dims[1])));
    }

    const float rawYes = input.tensors[0]->get(0);
    const float rawNo = input.tensors[0]->get(1);
    auto result = std::make_unique<open_perception_kit::metadata::PersonPresenceT>();
    result->object = open_perception_kit::makeObjectMeta(0U, input.inferenceInfo.parentId);
    if (input.inferenceInfo.parentId != 0U)
        result->subject_id = input.inferenceInfo.parentId;
    if (std::isfinite(rawYes) && std::isfinite(rawNo)) {
        result->result = std::make_unique<open_perception_kit::metadata::PersonPresenceResultT>();
        result->result->person_present = rawYes > rawNo;
        result->result->yes_score = rawYes;
        result->result->no_score = rawNo;
    }

    open_perception_kit::metadata::PersonPresencesT payload;
    payload.layer =
        open_perception_kit::makeLayerInfo({.model = input.inferenceInfo.modelName,
                                            .inferElementId = input.inferenceInfo.inferElementId,
                                            .producer = &input.producerInfo});
    payload.presences.push_back(std::move(result));
    results.add(std::move(payload));

    return {};
}
