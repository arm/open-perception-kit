/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

#include "postproc/CameraContactParser.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fmt/core.h>
#include <memory>
#include <utility>

using namespace opk;
using namespace opk::stdop::postproc;

namespace {

std::array<float, 2> softmax2(const opk::TensorView &tensor) {
    const float maxLogit = std::max(tensor.get(0), tensor.get(1));
    const float exp0 = std::exp(tensor.get(0) - maxLogit);
    const float exp1 = std::exp(tensor.get(1) - maxLogit);
    const float sum = exp0 + exp1;

    if (!(sum > 0.0f) || !std::isfinite(sum)) {
        return {0.5f, 0.5f};
    }

    return {exp0 / sum, exp1 / sum};
}

} // namespace

opk::Result<void> CameraContactParser::parse(const opk::TensorParser::Input &input,
                                             open_perception_kit::FrameResults &results) {
    if (!input.tensors[0]) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData, "CameraContactParser: input tensor is null"));
    }

    const auto &tensor = *input.tensors[0];
    const auto shape = tensor.getShape();

    if (shape.rank != 2 || shape.dims[0] != 1 || shape.dims[1] != 2) {
        return tl::unexpected(OPK_ERROR(
            opk::ErrorFlag::InvalidData,
            fmt::format("CameraContactParser expects [1,2] logits, got {}", shape.toString())));
    }

    const int contactClassIndex =
        static_cast<int>(input.attributes.getIntOrDefault("contactClassIndex", 1));
    const int noContactClassIndex =
        static_cast<int>(input.attributes.getIntOrDefault("noContactClassIndex", 0));

    if (contactClassIndex == noContactClassIndex || contactClassIndex < 0 ||
        contactClassIndex > 1 || noContactClassIndex < 0 || noContactClassIndex > 1) {
        return tl::unexpected(OPK_ERROR(
            opk::ErrorFlag::InvalidData,
            fmt::format(
                "CameraContactParser requires distinct class indices in [0,1], got {} and {}",
                contactClassIndex,
                noContactClassIndex)));
    }

    const auto probabilities = softmax2(tensor);
    const bool isContact = probabilities[static_cast<size_t>(contactClassIndex)] >=
                           probabilities[static_cast<size_t>(noContactClassIndex)];

    auto classification = std::make_unique<open_perception_kit::metadata::ClassificationT>();
    classification->object = open_perception_kit::makeObjectMeta(0U, input.inferenceInfo.parentId);

    auto candidate = std::make_unique<open_perception_kit::metadata::ClassificationCandidateT>();
    candidate->class_id = isContact ? contactClassIndex : noContactClassIndex;
    candidate->confidence = isContact ? probabilities[static_cast<size_t>(contactClassIndex)]
                                      : probabilities[static_cast<size_t>(noContactClassIndex)];
    candidate->text = isContact ? "contact" : "no contact";
    classification->candidates.push_back(std::move(candidate));

    open_perception_kit::metadata::ClassificationsT payload;
    payload.layer =
        open_perception_kit::makeLayerInfo({.model = input.inferenceInfo.modelName,
                                            .inferElementId = input.inferenceInfo.inferElementId,
                                            .contentType = k_content_type,
                                            .producer = &input.producerInfo});
    payload.classifications.push_back(std::move(classification));
    results.add(std::move(payload));

    return {};
}
