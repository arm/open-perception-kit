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

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace opk::classification {

inline bool validProbability(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

inline bool
validPresenceResult(const open_perception_kit::metadata::PersonPresenceResultT &result) noexcept {
    if (!result.yes_score || !result.no_score)
        return !result.yes_score && !result.no_score;
    return std::isfinite(*result.yes_score) && std::isfinite(*result.no_score);
}

inline std::optional<std::array<float, 2>> softmaxPair(const float first, const float second) {
    if (!std::isfinite(first) || !std::isfinite(second))
        return std::nullopt;
    const float maximum = std::max(first, second);
    const float a = std::exp(first - maximum);
    const float b = std::exp(second - maximum);
    const float sum = a + b;
    return std::array<float, 2>{a / sum, b / sum};
}

inline bool
validCandidate(const open_perception_kit::metadata::ClassificationT &classification,
               const open_perception_kit::metadata::ClassificationCandidateT &candidate) {
    if (candidate.confidence && !validProbability(*candidate.confidence))
        return false;
    using Kind = open_perception_kit::metadata::ClassificationKind;
    switch (classification.kind) {
    case Kind::Categorical:
        return !classification.label_set.empty() && candidate.class_id.has_value();
    case Kind::RecognizedText:
        return classification.label_set.empty() && !candidate.class_id.has_value();
    default:
        return false;
    }
}

} // namespace opk::classification
