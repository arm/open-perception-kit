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

#include "op/Op.h"
#include "op/OpChainDescriptor.h"

#include <array>
#include <fmt/format.h>

using namespace opk::op;

open_perception_kit::metadata::ProducerInfoT
Op::producerInfo(std::string_view inferElementId,
                 std::string_view implementation,
                 std::string_view fallbackComponent) const {
    open_perception_kit::metadata::ProducerInfoT result;
    result.instance_id = opk::producerInstanceId(inferElementId, instanceId);
    result.component = libName.empty() || opName.empty() ? fallbackComponent
                                                         : fmt::format("{}/{}", libName, opName);
    result.implementation = implementation;
    return result;
}

opk::Result<void>
opk::op::validateInputPreprocessors(const std::span<opk::op::Op *const> operations,
                                    const opk::op::InferenceInputBinding &imageBinding,
                                    const size_t inputCount) {
    std::array<bool, opk::MaxTensorCount> written{};
    const auto claimInputs =
        [&](const opk::op::InferenceInputBinding &binding) -> opk::Result<void> {
        if (binding.inferenceInstance != imageBinding.inferenceInstance ||
            binding.sourceName != imageBinding.sourceName || binding.tensorIndices.empty()) {
            return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                                            "Input preprocessors require matching inference/source "
                                            "bindings and declared inputs"));
        }
        for (const size_t tensorIndex : binding.tensorIndices) {
            if (tensorIndex >= inputCount || tensorIndex >= written.size() ||
                written[tensorIndex]) {
                return tl::unexpected(
                    OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                              "Input preprocessors require valid, nonoverlapping tensor inputs"));
            }
            written[tensorIndex] = true;
        }
        return {};
    };

    if (const auto result = claimInputs(imageBinding); !result) {
        return result;
    }

    for (const auto *const operation : operations) {
        const auto *const preprocessor =
            operation->as<opk::op::OpInterfaceInferenceInputPreprocessor>();
        if (operation->loopId != 0 || operation->as<opk::op::OpInterfaceInference>() ||
            !preprocessor) {
            return tl::unexpected(OPK_ERROR(
                opk::ErrorFlag::InvalidOpChain,
                "Explicit image binding permits only unlooped cooperating input preprocessors "
                "before inference"));
        }
        const auto binding = preprocessor->getInputBinding();
        if (const auto result = claimInputs(binding); !result) {
            return result;
        }
    }
    return {};
}

bool Op::supportsStageRole(const StageRole role) const {
    switch (role) {
    case StageRole::Controller:
        return as<OpInterfaceController>() != nullptr;
    case StageRole::Inference:
        return as<OpInterfaceInference>() != nullptr;
    case StageRole::AudioPreprocess:
        return as<OpInterfaceAudioPreprocessor>() != nullptr;
    case StageRole::ImagePreprocess:
        return as<OpInterfaceImageInputPreprocessor>() != nullptr;
    case StageRole::InputPreprocess:
        return as<OpInterfaceInferenceInputPreprocessor>() != nullptr;
    case StageRole::Postprocess:
        return as<OpInterfacePostprocessor>() != nullptr;
    case StageRole::IntermediateProcess:
        return true;
    }
    return false;
}
