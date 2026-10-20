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

#include "postproc/SileroVadParser.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace opk::stdop::postproc {

opk::Result<void> SileroVadParser::parse(const opk::TensorParser::Input &input,
                                         open_perception_kit::FrameResults &results) {
    auto invalid = [](const char *message) -> opk::Result<void> {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData, message));
    };
    if (input.attributes.contains("windowMs"))
        return invalid("SileroVadParser: windowMs is obsolete; timing comes from the audio hop");
    const auto *tensor = input.tensors[0];
    if (!tensor || !tensor->isValid() || tensor->getValueType() != opk::Dtype::Float32 ||
        !(tensor->getShape() == opk::Shape(1, 1)))
        return invalid("SileroVadParser requires a Float32 [1,1] probability tensor");

    if (input.attributes.contains("speechClassIndex"))
        return invalid("SileroVadParser has one speech probability; remove speechClassIndex");
    const auto thresholdOn = input.attributes.getDoubleOrDefault("thresholdOn", 0.60);
    const auto thresholdOff = input.attributes.getDoubleOrDefault("thresholdOff", 0.45);
    const auto attackMs = input.attributes.getIntOrDefault("attackMs", 50);
    const auto releaseMs = input.attributes.getIntOrDefault("releaseMs", 50);
    if (!std::isfinite(thresholdOn) || !std::isfinite(thresholdOff) || thresholdOff < 0 ||
        thresholdOn > 1 || thresholdOff > thresholdOn || attackMs < 0 || releaseMs < 0 ||
        uint64_t(attackMs) > std::numeric_limits<uint64_t>::max() / 16 ||
        uint64_t(releaseMs) > std::numeric_limits<uint64_t>::max() / 16)
        return invalid("SileroVadParser: invalid thresholds or attack/release duration");
    for (size_t i = 0; i < 1; ++i) {
        const auto value = tensor->get(i);
        if (!std::isfinite(value) || value < 0 || value > 1)
            return invalid("SileroVadParser expects finite sigmoid probabilities in [0,1]");
    }
    const auto &audio = input.inferenceInfo.audio;
    if (!audio.hopSampleCount || audio.windowEndSample <= audio.windowStartSample ||
        audio.hopSampleCount > audio.windowEndSample - audio.windowStartSample)
        return invalid("SileroVadParser requires a valid audio window and effective hop");
    if (audio.windowStartTimestampNs.has_value() != audio.windowEndTimestampNs.has_value() ||
        (audio.windowStartTimestampNs &&
         *audio.windowEndTimestampNs <= *audio.windowStartTimestampNs))
        return invalid("SileroVadParser requires ordered timestamps or an absent timestamp pair");

    const uint64_t attackLimit = uint64_t(attackMs) * 16;
    const uint64_t releaseLimit = uint64_t(releaseMs) * 16;
    const float probability = tensor->get(0);
    if (input.inferenceInfo.audio.discontinuity) {
        speechActive_ = false;
        attackSamples_ = releaseSamples_ = 0;
    }
    if (!speechActive_) {
        releaseSamples_ = 0;
        if (probability >= thresholdOn) {
            attackSamples_ = std::min(attackSamples_, attackLimit);
            attackSamples_ +=
                std::min<uint64_t>(audio.hopSampleCount, attackLimit - attackSamples_);
            if (attackSamples_ >= attackLimit) {
                speechActive_ = true;
                attackSamples_ = 0;
            }
        } else {
            attackSamples_ = 0;
        }
    } else {
        attackSamples_ = 0;
        if (probability <= thresholdOff) {
            releaseSamples_ = std::min(releaseSamples_, releaseLimit);
            releaseSamples_ +=
                std::min<uint64_t>(audio.hopSampleCount, releaseLimit - releaseSamples_);
            if (releaseSamples_ >= releaseLimit) {
                speechActive_ = false;
                releaseSamples_ = 0;
            }
        } else {
            releaseSamples_ = 0;
        }
    }

    auto detection = std::make_unique<open_perception_kit::metadata::VoiceActivityDetectionT>();
    detection->object = open_perception_kit::makeObjectMeta(0U, input.inferenceInfo.parentId);
    detection->speech_probability = probability;
    detection->speech_active = speechActive_;
    detection->window_start_sample = audio.windowStartSample;
    detection->window_end_sample = audio.windowEndSample;
    if (audio.windowStartTimestampNs) {
        detection->window_start_ts_ns = *audio.windowStartTimestampNs;
        detection->window_end_ts_ns = *audio.windowEndTimestampNs;
    }
    detection->discontinuity = input.inferenceInfo.audio.discontinuity;
    open_perception_kit::metadata::VoiceActivityDetectionsT payload;
    payload.layer =
        open_perception_kit::makeLayerInfo({.model = input.inferenceInfo.modelName,
                                            .inferElementId = input.inferenceInfo.inferElementId,
                                            .contentType = k_content_type,
                                            .producer = &input.producerInfo});
    payload.detections.push_back(std::move(detection));
    results.add(std::move(payload));
    return {};
}

} // namespace opk::stdop::postproc
