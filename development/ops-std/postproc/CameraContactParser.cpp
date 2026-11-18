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

#include "postproc/CameraContactParser.h"
#include "opk/ClassificationScores.h"

#include <fmt/core.h>
#include <memory>
#include <utility>

using namespace opk;
using namespace opk::stdop::postproc;

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

    const auto contactClassIndex = input.attributes.getIntOrDefault("contactClassIndex", 1);
    const auto noContactClassIndex = input.attributes.getIntOrDefault("noContactClassIndex", 0);

    if (contactClassIndex == noContactClassIndex || contactClassIndex < 0 ||
        contactClassIndex > 1 || noContactClassIndex < 0 || noContactClassIndex > 1) {
        return tl::unexpected(OPK_ERROR(
            opk::ErrorFlag::InvalidData,
            fmt::format(
                "CameraContactParser requires distinct class indices in [0,1], got {} and {}",
                contactClassIndex,
                noContactClassIndex)));
    }

    auto classification = std::make_unique<open_perception_kit::metadata::CameraContactT>();
    classification->object = open_perception_kit::makeObjectMeta(0U, input.inferenceInfo.parentId);
    if (input.inferenceInfo.parentId != 0U)
        classification->subject_id = input.inferenceInfo.parentId;
    const auto probabilities = opk::classification::softmaxPair(tensor.get(0), tensor.get(1));
    if (probabilities) {
        const bool isContact = (*probabilities)[static_cast<size_t>(contactClassIndex)] >=
                               (*probabilities)[static_cast<size_t>(noContactClassIndex)];
        auto candidate = std::make_unique<open_perception_kit::metadata::CameraContactResultT>();
        const auto selected =
            static_cast<std::uint32_t>(isContact ? contactClassIndex : noContactClassIndex);
        candidate->in_contact = isContact;
        candidate->model_class_index = selected;
        candidate->confidence = (*probabilities)[selected];
        classification->result = std::move(candidate);
    }

    open_perception_kit::metadata::CameraContactsT payload;
    payload.layer =
        open_perception_kit::makeLayerInfo({.model = input.inferenceInfo.modelName,
                                            .inferElementId = input.inferenceInfo.inferElementId,
                                            .producer = &input.producerInfo});
    payload.contacts.push_back(std::move(classification));
    results.add(std::move(payload));

    return {};
}
