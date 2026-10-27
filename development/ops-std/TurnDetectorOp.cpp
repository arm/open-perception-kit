/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

#include "TurnDetectorOp.h"

#include <algorithm>

using namespace opk::stdop;

opk::Result<void> TurnDetectorOp::configure(const opk::AttributeMap &attributes) {
    const auto hop = attributes.getIntOrDefault("hopSampleCount", 512);
    const auto silenceMs = attributes.getIntOrDefault("silenceMs", 300);
    const auto retryMs = attributes.getIntOrDefault("retryMs", 500);
    const auto count = attributes.getIntOrDefault("maxAttempts", 3);
    inputAudioSourceName =
        attributes.getStringOrDefault("inputAudioSourceName", "pipelineAudioData");
    vadModel = attributes.getStringOrDefault("vadModel", "silero-vad");
    if (hop <= 0 || hop > 960000 || silenceMs < 0 || silenceMs > 60000 || retryMs <= 0 ||
        retryMs > 60000 || count <= 0 || count > 100 || inputAudioSourceName.empty() ||
        vadModel.empty())
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain, "TurnDetector: invalid configuration"));

    hopSampleCount = static_cast<size_t>(hop);
    silenceSamples = static_cast<uint64_t>(silenceMs) * opk::PcmRingBuffer::frequency / 1000;
    retrySamples = static_cast<uint64_t>(retryMs) * opk::PcmRingBuffer::frequency / 1000;
    maxAttempts = static_cast<size_t>(count);
    inference = nullptr;
    history.reset();
    nextPosition.reset();
    lastSpeechEnd.reset();
    lastAttemptEnd.reset();
    attempts = 0;
    return {};
}

opk::Result<void> TurnDetectorOp::bind(size_t index, const std::vector<opk::op::Op *> &ops) {
    inference = nullptr;
    if (index == 0 || index >= ops.size() || ops.size() - index < 2 || !ops[index - 1] ||
        !ops[index + 1] || loopId != 0)
        return tl::unexpected(OPK_ERROR(
            opk::ErrorFlag::InvalidOpChain,
            "TurnDetector must follow VAD postprocessing and precede inference without a loop"));
    const auto *post = ops[index - 1]->as<opk::op::OpInterfacePostprocessor>();
    const auto *next = ops[index + 1]->as<opk::op::OpInterfaceInference>();
    if (!post || !next)
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                      "TurnDetector requires adjacent VAD postprocess/inference"));
    const auto provided = post->getProvidedContentTypes();
    if (std::ranges::find(provided, "voiceActivity") == provided.end())
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                                        "TurnDetector requires preceding voiceActivity output"));

    const auto &inputs = next->getModel().inputs;
    if (inputs.size() != 1)
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain, "TurnDetector requires one PCM model input"));
    const auto &input = inputs[0];
    if (input.dataKind != opk::DataKind::AudioPcm || input.valueType != opk::Dtype::Float32 ||
        input.shape.rank != 2 || input.shape.dims[0] != 1 || input.shape.dims[1] <= 0 ||
        input.shape.dims[1] > 960000 || input.matchShapeOutputIndex != opk::InvalidTensorIndex)
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                                        "TurnDetector requires a fixed Float32 [1,N] PCM input"));
    const auto samples = static_cast<size_t>(input.shape.dims[1]);
    // Assessments happen on whole silent hops, including at least one for zero delay.
    const uint64_t silenceHops =
        std::max(uint64_t{1}, (silenceSamples + hopSampleCount - 1) / hopSampleCount);
    if (silenceHops * hopSampleCount >= samples)
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidOpChain,
                      "TurnDetector silence rounded up to full hops must be shorter than the "
                      "model window"));
    history = std::make_unique<opk::PcmRingBuffer>(samples);
    reset(0);
    inference = next;
    return {};
}

void TurnDetectorOp::reset(uint64_t position) {
    history->clear(position);
    nextPosition.reset();
    lastSpeechEnd.reset();
    lastAttemptEnd.reset();
    attempts = 0;
    pendingDiscontinuity = true;
}

opk::Result<opk::op::OpSignal> TurnDetectorOp::process(opk::op::OpChainContext &context) {
    using opk::op::OpSignal;
    const auto invalid = [](const char *message) -> opk::Result<OpSignal> {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData, message));
    };
    if (!inference || !history || !context.frameResults)
        return invalid("TurnDetector is not bound or has no FrameResults");
    const auto *window = context.getPcmView(inputAudioSourceName);
    auto &audio = context.inferenceInfo.audio;
    if (!window || window->size() != hopSampleCount || audio.hopSampleCount != hopSampleCount ||
        audio.windowStartSample != window->beginPosition() ||
        audio.windowEndSample != window->endPosition())
        return invalid("TurnDetector requires contiguous, nonoverlapping PCM hops and timing");

    // A GstBuffer may already contain earlier VAD windows. Match this exact window
    // and producer instead of reading the first/last voiceActivity payload blindly.
    std::optional<bool> speech;
    size_t matches = 0;
    context.frameResults->for_each<open_perception_kit::metadata::VoiceActivityDetectionsT>(
        [&](const auto &payload) {
            if (!payload.layer || payload.layer->model != vadModel ||
                payload.layer->infer_element_id != context.inferenceInfo.inferElementId)
                return;
            for (const auto &detection : payload.detections) {
                if (detection && detection->window_start_sample == audio.windowStartSample &&
                    detection->window_end_sample == audio.windowEndSample) {
                    speech = detection->speech_active;
                    ++matches;
                }
            }
        });
    if (matches != 1 || !speech.has_value())
        return invalid("TurnDetector requires exactly one VAD result for the current PCM hop");

    if (audio.discontinuity || !nextPosition || *nextPosition != window->beginPosition())
        reset(window->beginPosition());
    if (!history->append(window->first()) || !history->append(window->second()))
        return invalid("TurnDetector could not retain PCM history");
    nextPosition = window->endPosition();

    if (*speech) {
        lastSpeechEnd = window->endPosition();
        lastAttemptEnd.reset();
        attempts = 0;
        return OpSignal::AbortChain;
    }
    if (!lastSpeechEnd || *lastSpeechEnd <= history->beginPosition() ||
        window->endPosition() - *lastSpeechEnd < silenceSamples || attempts >= maxAttempts ||
        (lastAttemptEnd && window->endPosition() - *lastAttemptEnd < retrySamples))
        return OpSignal::AbortChain;

    auto *const destination = inference->getTensorDataAddress(0);
    const size_t bytes = history->capacity() * sizeof(float);
    if (!destination || !history->copyLatestPadded(std::span<uint8_t>{destination, bytes}))
        return invalid("TurnDetector could not prepare the model PCM input");
    lastAttemptEnd = window->endPosition();
    ++attempts;

    context.inferenceInfo.modelName = inference->getModel().name;
    context.inferenceInfo.contentType = inference->getModel().contentType;
    context.inferenceInfo.parentId = 0;
    audio.windowStartSample = history->beginPosition();
    audio.windowEndSample = history->endPosition();
    // A reset can precede this assessment by many skipped VAD hops.
    audio.discontinuity = pendingDiscontinuity;
    pendingDiscontinuity = false;
    // Positions/timestamps cover real retained samples; front-padding has no media PTS.
    const uint64_t durationNs =
        static_cast<uint64_t>(history->size()) * 1'000'000'000 / opk::PcmRingBuffer::frequency;
    if (audio.windowEndTimestampNs && *audio.windowEndTimestampNs >= durationNs) {
        audio.windowStartTimestampNs = *audio.windowEndTimestampNs - durationNs;
    } else {
        audio.windowStartTimestampNs.reset();
        audio.windowEndTimestampNs.reset();
    }
    return OpSignal::Continue;
}
