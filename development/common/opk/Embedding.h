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
#include <cmath>
#include <optional>
#include <span>

namespace opk::embedding {

inline std::optional<double> norm(const std::span<const float> values) noexcept {
    double sum = 0;
    for (const float value : values) {
        if (!std::isfinite(value))
            return std::nullopt;
        sum += static_cast<double>(value) * value;
    }
    // Double accumulation covers the entire Float32 range, including subnormals.
    return sum > 0 ? std::optional<double>(std::sqrt(sum)) : std::nullopt;
}

inline std::optional<double>
norm(const open_perception_kit::metadata::ObjectEmbeddingT &value) noexcept {
    if (!value.space || value.space->id.empty() || value.space->dimensions != value.values.size() ||
        value.space->metric != open_perception_kit::metadata::EmbeddingMetric::Cosine)
        return std::nullopt;
    return norm(value.values);
}

inline bool valid(const open_perception_kit::metadata::ObjectEmbeddingT &value) noexcept {
    return norm(value).has_value();
}

inline std::optional<float>
cosineSimilarity(const open_perception_kit::metadata::ObjectEmbeddingT &a,
                 const open_perception_kit::metadata::ObjectEmbeddingT &b) noexcept {
    const auto normA = norm(a);
    const auto normB = norm(b);
    if (!normA || !normB || a.space->id != b.space->id ||
        a.space->dimensions != b.space->dimensions || a.space->metric != b.space->metric)
        return std::nullopt;
    double dot = 0;
    for (size_t index = 0; index < a.values.size(); ++index)
        dot += static_cast<double>(a.values[index]) * b.values[index];
    return static_cast<float>(std::clamp(dot / (*normA * *normB), -1.0, 1.0));
}
} // namespace opk::embedding
