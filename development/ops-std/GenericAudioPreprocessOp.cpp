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

#include <algorithm>
#include <cstring>

using namespace opk::stdop;

opk::Result<void> GenericAudioPreprocessOp::configure(const opk::AttributeMap &attributes) {
    inference = nullptr;
    sampleCount = 0;
    contextSamples.clear();
    nextPosition.reset();
    const auto count = attributes.getIntOrDefault("contextSampleCount", 0);
    if (count < 0 || count > 2147483647)
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain, "Invalid audio context sample count"));
    contextSampleCount = static_cast<size_t>(count);
    inputAudioSourceName =
        attributes.getStringOrDefault("inputAudioSourceName", "pipelineAudioData");
    const auto tensorIndex = attributes.getIntOrDefault("inputAudioTensorIndex", -1);
    if (inputAudioSourceName.empty() || tensorIndex < -1) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                                        "GenericAudioPreprocess requires a source name and a "
                                        "nonnegative tensor index (or -1 for auto)"));
    }
    inputAudioTensorIndex =
        tensorIndex == -1 ? opk::InvalidTensorIndex : static_cast<size_t>(tensorIndex);
    return {};
}

opk::Result<void> GenericAudioPreprocessOp::bind(size_t index,
                                                 const std::vector<opk::op::Op *> &ops) {
    inference = nullptr;
    if (index >= ops.size() || ops.size() - index < 2 || !ops[index + 1]) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                      "GenericAudioPreprocess must be immediately before an inference Op"));
    }
    const auto *next = ops[index + 1]->as<opk::op::OpInterfaceInference>();
    if (!next) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                      "GenericAudioPreprocess must be immediately before an inference Op"));
    }
    const auto &inputs = next->getModel().inputs;
    size_t selected = inputAudioTensorIndex;
    if (selected == opk::InvalidTensorIndex) {
        for (size_t i = 0; i < inputs.size(); ++i) {
            if (inputs[i].dataKind != opk::DataKind::AudioPcm)
                continue;
            if (selected != opk::InvalidTensorIndex) {
                return tl::unexpected(
                    OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                              "Multiple AudioPcm inputs: specify inputAudioTensorIndex"));
            }
            selected = i;
        }
    }
    if (selected >= inputs.size() || selected >= opk::MaxTensorCount) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                      "GenericAudioPreprocess cannot find the selected AudioPcm input"));
    }
    const auto &input = inputs[selected];
    const auto &shape = input.shape;
    if (input.dataKind != opk::DataKind::AudioPcm || input.valueType != opk::Dtype::Float32 ||
        shape.rank == 0 || shape.rank > 8 || shape.dims[shape.rank - 1] <= 0 ||
        input.matchShapeOutputIndex != opk::InvalidTensorIndex) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                                        "GenericAudioPreprocess requires a fixed Float32 AudioPcm "
                                        "tensor; log-mel is not supported"));
    }
    for (size_t i = 0; i + 1 < shape.rank; ++i) {
        if (shape.dims[i] != 1) {
            return tl::unexpected(OPK_ERROR(
                opk::ErrorFlag::InvalidOpChain,
                "AudioPcm requires singleton leading dimensions and a final sample dimension"));
        }
    }
    inputAudioTensorIndex = selected;
    sampleCount = static_cast<size_t>(shape.dims[shape.rank - 1]);
    if (contextSampleCount >= sampleCount || contextSampleCount > sampleCount - contextSampleCount)
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                                        "Audio context must fit within the fresh sample window"));
    contextSamples.assign(contextSampleCount, 0.0f);
    nextPosition.reset();
    inference = next;
    return {};
}

opk::Result<opk::op::OpSignal> GenericAudioPreprocessOp::process(opk::op::OpChainContext &context) {
    if (!inference) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain, "GenericAudioPreprocess is not bound"));
    }
    const auto *window = context.getPcmView(inputAudioSourceName);
    if (!window || window->size() != getRequiredAudioSamples()) {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                        "GenericAudioPreprocess requires one complete scheduled "
                                        "PCM window matching the tensor"));
    }
    auto *destination = inference->getTensorDataAddress(inputAudioTensorIndex);
    if (!destination) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "GenericAudioPreprocess input tensor has no writable storage"));
    }
    if (!contextSamples.empty()) {
        const auto &audio = context.inferenceInfo.audio;
        if (audio.hopSampleCount != getRequiredAudioSamples() ||
            (!audio.discontinuity && nextPosition && *nextPosition != window->beginPosition()))
            return tl::unexpected(
                OPK_ERROR(opk::ErrorFlag::InvalidData,
                          "Context audio requires contiguous nonoverlapping windows"));
        if (audio.discontinuity)
            std::fill(contextSamples.begin(), contextSamples.end(), 0.0f);
        std::memcpy(destination, contextSamples.data(), contextSamples.size() * sizeof(float));
    }
    auto *fresh = destination + contextSamples.size() * sizeof(float);
    // Copy bytes so the backend's tensor pointer need not be float-aligned.
    const auto first = window->first();
    const auto second = window->second();
    if (!first.empty())
        std::memcpy(fresh, first.data(), first.size_bytes());
    if (!second.empty())
        std::memcpy(fresh + first.size_bytes(), second.data(), second.size_bytes());
    if (!contextSamples.empty()) {
        std::memcpy(contextSamples.data(),
                    destination + (sampleCount - contextSamples.size()) * sizeof(float),
                    contextSamples.size() * sizeof(float));
        nextPosition = window->endPosition();
    }
    const auto &model = inference->getModel();
    context.inferenceInfo.modelName = model.name;
    context.inferenceInfo.contentType = model.contentType;
    context.inferenceInfo.parentId = 0;
    // Preserve the scheduler's hop, sample positions and timestamps.
    return opk::op::OpSignal::Continue;
}
