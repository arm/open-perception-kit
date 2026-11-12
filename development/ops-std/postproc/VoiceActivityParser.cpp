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

#include "postproc/VoiceActivityParser.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace opk::stdop::postproc {
namespace {

struct Configuration {
    size_t tensorIndex;
    int64_t scoreIndex;
    bool sigmoid;
    double thresholdOn;
    double thresholdOff;
    int64_t attackMs;
    int64_t releaseMs;
};

auto invalidInput(const char *const message) {
    return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData, message));
}

opk::Result<Configuration> readConfiguration(const opk::AttributeMap &attributes) {
    for (const auto *const name : {"outputTensorIndex", "scoreIndex", "attackMs", "releaseMs"}) {
        if (!attributes.contains(name) || !attributes.at(name).isInt())
            return invalidInput(
                "VoiceActivityParser requires integer tensor/score indices and durations");
    }
    for (const auto *const name : {"thresholdOn", "thresholdOff"}) {
        if (!attributes.contains(name) ||
            (!attributes.at(name).isInt() && !attributes.at(name).isDouble()))
            return invalidInput("VoiceActivityParser requires numeric thresholds");
    }
    if (!attributes.contains("activation") || !attributes.at("activation").isString())
        return invalidInput("VoiceActivityParser requires an explicit activation");
    const auto &activation = attributes.getString("activation");
    if (activation != "sigmoid" && activation != "none")
        return invalidInput("VoiceActivityParser activation must be sigmoid or none");
    const auto tensorIndex = attributes.getInt("outputTensorIndex");
    if (tensorIndex < 0 || static_cast<uint64_t>(tensorIndex) >= opk::MaxTensorCount)
        return invalidInput(
            "VoiceActivityParser outputTensorIndex is outside the output tensor array");
    return Configuration{.tensorIndex = static_cast<size_t>(tensorIndex),
                         .scoreIndex = attributes.getInt("scoreIndex"),
                         .sigmoid = activation == "sigmoid",
                         .thresholdOn = attributes.getDouble("thresholdOn"),
                         .thresholdOff = attributes.getDouble("thresholdOff"),
                         .attackMs = attributes.getInt("attackMs"),
                         .releaseMs = attributes.getInt("releaseMs")};
}

opk::Result<float> decodeProbability(const opk::TensorParser::Input &input,
                                     const Configuration &configuration) {
    const auto *const tensor = input.tensors[configuration.tensorIndex];
    if (!tensor || !tensor->isValid() || tensor->getValueType() != opk::Dtype::Float32)
        return invalidInput("VoiceActivityParser requires a valid Float32 output tensor");
    const auto &shape = tensor->getShape();
    if (shape.rank == 0 || shape.rank > 8)
        return invalidInput("VoiceActivityParser requires a nonempty fixed tensor shape");
    size_t valueCount = 1;
    const size_t capacity = tensor->getByteCount() / sizeof(float);
    for (size_t dimension = 0; dimension < shape.rank; ++dimension) {
        if (shape.dims[dimension] <= 0 ||
            static_cast<size_t>(shape.dims[dimension]) > capacity / valueCount)
            return invalidInput("VoiceActivityParser tensor shape exceeds its storage");
        valueCount *= static_cast<size_t>(shape.dims[dimension]);
    }
    if (configuration.scoreIndex < 0 ||
        static_cast<uint64_t>(configuration.scoreIndex) >= valueCount)
        return invalidInput("VoiceActivityParser scoreIndex is outside the output tensor");
    const float score = tensor->get(static_cast<size_t>(configuration.scoreIndex));
    if (!std::isfinite(score))
        return invalidInput("VoiceActivityParser requires a finite score");
    if (!configuration.sigmoid)
        return score;
    const float exponential = std::exp(-std::abs(score));
    return score >= 0 ? 1.0f / (1.0f + exponential) : exponential / (1.0f + exponential);
}

opk::Result<void> validateDecision(const Configuration &configuration,
                                   const float probability,
                                   const opk::AudioInferenceMetadata &audio) {
    const auto thresholdOn = configuration.thresholdOn;
    const auto thresholdOff = configuration.thresholdOff;
    const auto attackMs = configuration.attackMs;
    const auto releaseMs = configuration.releaseMs;
    if (!std::isfinite(thresholdOn) || !std::isfinite(thresholdOff) || thresholdOff < 0 ||
        thresholdOn > 1 || thresholdOff > thresholdOn || attackMs < 0 || releaseMs < 0 ||
        static_cast<uint64_t>(attackMs) > std::numeric_limits<uint64_t>::max() / 16 ||
        static_cast<uint64_t>(releaseMs) > std::numeric_limits<uint64_t>::max() / 16)
        return invalidInput("VAD parser: invalid thresholds or attack/release duration");
    if (!std::isfinite(probability) || probability < 0 || probability > 1)
        return invalidInput("VAD parser expects finite probabilities in [0,1]");
    if (!audio.hopSampleCount || audio.windowEndSample <= audio.windowStartSample ||
        audio.hopSampleCount > audio.windowEndSample - audio.windowStartSample)
        return invalidInput("VAD parser requires a valid audio window and effective hop");
    if (audio.windowStartTimestampNs.has_value() != audio.windowEndTimestampNs.has_value() ||
        (audio.windowStartTimestampNs &&
         *audio.windowEndTimestampNs <= *audio.windowStartTimestampNs))
        return invalidInput("VAD parser requires ordered timestamps or an absent timestamp pair");
    return {};
}

open_perception_kit::metadata::VoiceActivityDetectionsT makeResult(
    const opk::TensorParser::Input &input, const float probability, const bool speechActive) {
    const auto &audio = input.inferenceInfo.audio;
    auto detection = std::make_unique<open_perception_kit::metadata::VoiceActivityDetectionT>();
    detection->object = open_perception_kit::makeObjectMeta(0U, input.inferenceInfo.parentId);
    detection->speech_probability = probability;
    detection->speech_active = speechActive;
    detection->window_start_sample = audio.windowStartSample;
    detection->window_end_sample = audio.windowEndSample;
    detection->window_start_ts_ns = audio.windowStartTimestampNs;
    detection->window_end_ts_ns = audio.windowEndTimestampNs;
    detection->discontinuity = audio.discontinuity;
    open_perception_kit::metadata::VoiceActivityDetectionsT payload;
    payload.layer =
        open_perception_kit::makeLayerInfo({.model = input.inferenceInfo.modelName,
                                            .inferElementId = input.inferenceInfo.inferElementId,
                                            .contentType = VoiceActivityParser::k_content_type,
                                            .producer = &input.producerInfo});
    payload.detections.push_back(std::move(detection));
    return payload;
}

} // namespace

void VoiceActivityParser::advanceTransition(const bool requested,
                                            const uint64_t delaySamples,
                                            const uint64_t hopSamples) {
    if (!requested) {
        pendingSamples_ = 0;
        return;
    }
    pendingSamples_ = std::min(pendingSamples_, delaySamples);
    pendingSamples_ += std::min(hopSamples, delaySamples - pendingSamples_);
    if (pendingSamples_ >= delaySamples) {
        speechActive_ = !speechActive_;
        pendingSamples_ = 0;
    }
}

opk::Result<void> VoiceActivityParser::parse(const opk::TensorParser::Input &input,
                                             open_perception_kit::FrameResults &results) {
    const auto configuration = readConfiguration(input.attributes);
    if (!configuration)
        return tl::unexpected(configuration.error());
    const auto probability = decodeProbability(input, *configuration);
    if (!probability)
        return tl::unexpected(probability.error());
    const auto &audio = input.inferenceInfo.audio;
    if (const auto valid = validateDecision(*configuration, *probability, audio); !valid)
        return tl::unexpected(valid.error());

    if (audio.discontinuity) {
        speechActive_ = false;
        pendingSamples_ = 0;
    }
    const bool requested = speechActive_ ? *probability <= configuration->thresholdOff
                                         : *probability >= configuration->thresholdOn;
    const auto delayMs = speechActive_ ? configuration->releaseMs : configuration->attackMs;
    advanceTransition(requested, static_cast<uint64_t>(delayMs) * 16, audio.hopSampleCount);
    results.add(makeResult(input, *probability, speechActive_));
    return {};
}

} // namespace opk::stdop::postproc
