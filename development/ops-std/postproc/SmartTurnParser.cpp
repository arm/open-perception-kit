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

#include "postproc/SmartTurnParser.h"

#include <cmath>

opk::Result<void>
opk::stdop::postproc::SmartTurnParser::parse(const opk::TensorParser::Input &input,
                                             open_perception_kit::FrameResults &results) {
    const auto *tensor = input.tensors[0];
    if (!tensor || !tensor->isValid() || tensor->getValueType() != opk::Dtype::Float32 ||
        !(tensor->getShape() == opk::Shape(1, 1)))
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                        "SmartTurnParser requires a Float32 [1,1] probability"));
    const float probability = tensor->get(0);
    const auto threshold = input.attributes.getDoubleOrDefault("threshold", 0.5);
    if (!std::isfinite(probability) || probability < 0 || probability > 1 ||
        !std::isfinite(threshold) || threshold < 0 || threshold > 1)
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData,
                                        "SmartTurnParser requires probability/threshold in [0,1]"));

    const auto &audio = input.inferenceInfo.audio;
    if (!audio.hopSampleCount || audio.windowEndSample <= audio.windowStartSample ||
        audio.hopSampleCount > audio.windowEndSample - audio.windowStartSample)
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "SmartTurnParser requires a valid audio window and effective hop"));
    if (audio.windowStartTimestampNs.has_value() != audio.windowEndTimestampNs.has_value() ||
        (audio.windowStartTimestampNs &&
         *audio.windowEndTimestampNs <= *audio.windowStartTimestampNs))
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      "SmartTurnParser requires ordered timestamps or an absent timestamp pair"));

    auto detection = std::make_unique<open_perception_kit::metadata::TurnDetectionT>();
    detection->object = open_perception_kit::makeObjectMeta(0U, input.inferenceInfo.parentId);
    detection->turn_probability = probability;
    detection->turn_complete = probability >= threshold;
    detection->window_start_sample = audio.windowStartSample;
    detection->window_end_sample = audio.windowEndSample;
    detection->window_start_ts_ns = audio.windowStartTimestampNs;
    detection->window_end_ts_ns = audio.windowEndTimestampNs;
    detection->discontinuity = audio.discontinuity;
    open_perception_kit::metadata::TurnDetectionsT payload;
    payload.layer =
        open_perception_kit::makeLayerInfo({.model = input.inferenceInfo.modelName,
                                            .inferElementId = input.inferenceInfo.inferElementId,
                                            .contentType = k_content_type,
                                            .producer = &input.producerInfo});
    payload.detections.push_back(std::move(detection));
    results.add(std::move(payload));
    return {};
}
