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

#include "opk/Result.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace opk {

// The caller validates nonzero dimensions, index < outputSize and multiplication bounds.
constexpr size_t
nearestImageSample(const size_t index, const size_t inputSize, const size_t outputSize) {
    return index * inputSize / outputSize;
}

struct LinearImageSample {
    size_t first;
    size_t second;
    float weight;
};

// Half-pixel centres with replicated edges, also usable for compile-time sampling tables.
constexpr LinearImageSample
linearImageSample(const size_t index, const size_t inputSize, const size_t outputSize) {
    const float coordinate =
        std::clamp((static_cast<float>(index) + 0.5F) * static_cast<float>(inputSize) /
                           static_cast<float>(outputSize) -
                       0.5F,
                   0.0F,
                   static_cast<float>(inputSize - 1));
    const auto first = static_cast<size_t>(coordinate);
    return {first, std::min(first + 1, inputSize - 1), coordinate - first};
}

// Source dimensions and samples must already be validated by the caller.
inline float interpolateImage(const std::span<const float> pixels,
                              const size_t stride,
                              const LinearImageSample &x,
                              const LinearImageSample &y) {
    const float upper = (1.0F - x.weight) * pixels[y.first * stride + x.first] +
                        x.weight * pixels[y.first * stride + x.second];
    const float lower = (1.0F - x.weight) * pixels[y.second * stride + x.first] +
                        x.weight * pixels[y.second * stride + x.second];
    return (1.0F - y.weight) * upper + y.weight * lower;
}

/// Resize interleaved uint8 pixels (1, 3 or 4 channels), preserving channel order.
/// Uses half-pixel centres, replicated edges and fixed-point uint8 rounding.
Result<std::vector<uint8_t>> resizeBilinear(const std::span<const uint8_t> pixels,
                                            const size_t width,
                                            const size_t height,
                                            const size_t stride,
                                            const size_t channels,
                                            const size_t outputWidth,
                                            const size_t outputHeight);

/// Resize a tightly packed Float32 plane using half-pixel centres and replicated edges.
Result<std::vector<float>> resizeBilinear(const std::span<const float> pixels,
                                          const size_t width,
                                          const size_t height,
                                          const size_t outputWidth,
                                          const size_t outputHeight);

} // namespace opk
