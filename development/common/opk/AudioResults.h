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
#pragma once
#include "open_perception_kit.h"
#include <cmath>

namespace opk::audio_results {
template <typename Result> bool validWindow(const Result &value) noexcept {
    if (value.window_end_sample <= value.window_start_sample)
        return false;
    if (!value.window_start_ts_ns)
        return !value.window_end_ts_ns;
    return value.window_end_ts_ns && *value.window_end_ts_ns > *value.window_start_ts_ns;
}
inline bool valid(const open_perception_kit::metadata::VoiceActivityDetectionT &value) noexcept {
    return validWindow(value) && value.speech_active.has_value() && value.speech_probability &&
           std::isfinite(*value.speech_probability) && *value.speech_probability >= 0 &&
           *value.speech_probability <= 1;
}
inline bool valid(const open_perception_kit::metadata::TurnDetectionT &value) noexcept {
    return validWindow(value) && value.turn_complete.has_value() && value.turn_probability &&
           std::isfinite(*value.turn_probability) && *value.turn_probability >= 0 &&
           *value.turn_probability <= 1;
}
} // namespace opk::audio_results
