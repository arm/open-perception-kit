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

#include "GenericAudioPreprocessOp.h"

using namespace opk::stdop;

namespace {
opk::Result<size_t> selectAudioInput(const std::vector<opk::ModelInput> &inputs,
                                     const size_t requestedIndex) {
    size_t selected = requestedIndex;
    if (selected == opk::InvalidTensorIndex) {
        for (size_t i = 0; i < inputs.size(); ++i) {
            if (inputs[i].dataKind != opk::DataKind::AudioPcm &&
                inputs[i].dataKind != opk::DataKind::AudioLogMel)
                continue;
            if (selected != opk::InvalidTensorIndex) {
                return tl::unexpected(
                    OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                              "Multiple audio inputs: specify inputAudioTensorIndex"));
            }
            selected = i;
        }
    }
    if (selected >= inputs.size() || selected >= opk::MaxTensorCount) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                      "GenericAudioPreprocess cannot find the selected audio input"));
    }
    return selected;
}
} // namespace

opk::Result<void> GenericAudioPreprocessOp::configure(const opk::AttributeMap &attributes) {
    inference = nullptr;
    tensorBuilder.resetBinding();
    inputAudioSourceName.clear();
    if (attributes.contains("inputAudioTensorIndex") &&
        !attributes.at("inputAudioTensorIndex").isInt())
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain, "Audio tensor index must be an integer"));
    if (attributes.contains("inputAudioSourceName") &&
        !attributes.at("inputAudioSourceName").isString())
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain, "Audio source name must be a string"));
    const auto sourceName =
        attributes.getStringOrDefault("inputAudioSourceName", "pipelineAudioData");
    const auto tensorIndex = attributes.getIntOrDefault("inputAudioTensorIndex", -1);
    if (sourceName.empty() || tensorIndex < -1) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                                        "GenericAudioPreprocess requires a source name and a "
                                        "nonnegative tensor index (or -1 for auto)"));
    }
    if (const auto result = tensorBuilder.configure(attributes); !result)
        return result;
    inputAudioSourceName = sourceName;
    inputAudioTensorIndex =
        tensorIndex == -1 ? opk::InvalidTensorIndex : static_cast<size_t>(tensorIndex);
    return {};
}

opk::Result<void> GenericAudioPreprocessOp::bind(const size_t index,
                                                 const std::vector<opk::op::Op *> &ops) {
    inference = nullptr;
    tensorBuilder.resetBinding();
    if (inputAudioSourceName.empty())
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                                        "GenericAudioPreprocess has no valid configuration"));
    if (index >= ops.size() || ops.size() - index < 2 || !ops[index + 1]) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                      "GenericAudioPreprocess must be immediately before an inference Op"));
    }
    const auto *const next = ops[index + 1]->as<opk::op::OpInterfaceInference>();
    if (!next) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                      "GenericAudioPreprocess must be immediately before an inference Op"));
    }
    const auto selected = selectAudioInput(next->getModel().inputs, inputAudioTensorIndex);
    if (!selected)
        return tl::unexpected(selected.error());
    if (const auto result = tensorBuilder.bind(next->getModel().inputs[*selected]); !result)
        return result;
    boundAudioTensorIndex = *selected;
    inference = next;
    return {};
}

opk::Result<opk::op::OpSignal> GenericAudioPreprocessOp::process(opk::op::OpChainContext &context) {
    if (!inference) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain, "GenericAudioPreprocess is not bound"));
    }
    const auto *const window = context.getPcmView(inputAudioSourceName);
    if (!window)
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "GenericAudioPreprocess requires its scheduled PCM source"));
    auto *const destination = inference->getTensorDataAddress(boundAudioTensorIndex);
    const auto &audio = context.inferenceInfo.audio;
    if (const auto result =
            tensorBuilder.build(*window, destination, audio.hopSampleCount, audio.discontinuity);
        !result)
        return tl::unexpected(result.error());
    const auto &model = inference->getModel();
    context.inferenceInfo.modelName = model.name;
    context.inferenceInfo.contentType = model.contentType;
    context.inferenceInfo.parentId = 0;
    // Preserve the scheduler's hop, sample positions and timestamps.
    return opk::op::OpSignal::Continue;
}
