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

#include "opk/ImageResize.h"
#include <cmath>
#include <limits>

namespace opk {
namespace {
bool validImage(const size_t count,
                const size_t width,
                const size_t height,
                const size_t stride,
                const size_t channels,
                const size_t outputWidth,
                const size_t outputHeight) {
    const size_t limit = std::numeric_limits<size_t>::max();
    return width != 0 && height != 0 && outputWidth != 0 && outputHeight != 0 &&
           (channels == 1 || channels == 3 || channels == 4) && width <= limit / channels &&
           stride >= width * channels && height - 1 <= (limit - width * channels) / stride &&
           count >= (height - 1) * stride + width * channels && outputWidth <= limit / channels &&
           outputHeight <= limit / (outputWidth * channels);
}

struct IntegerSample {
    size_t first;
    size_t second;
    int firstWeight;
    int secondWeight;
};

// Eleven fractional bits preserve the qualified uint8 interpolation contract.
constexpr int WeightBits = 11;
constexpr int WeightScale = 1 << WeightBits;
constexpr int HorizontalDiscardBits = 4;
constexpr int VerticalDiscardBits = 16;
constexpr int OutputFractionBits = 2 * WeightBits - HorizontalDiscardBits - VerticalDiscardBits;
constexpr int OutputScale = 1 << OutputFractionBits;

IntegerSample integerSample(const size_t index, const size_t inputSize, const size_t outputSize) {
    const float coordinate = static_cast<float>(
        (static_cast<double>(index) + 0.5) * (static_cast<double>(inputSize) / outputSize) - 0.5);
    const float lower = std::floor(coordinate);
    const float weight = coordinate - lower;
    const auto first =
        static_cast<size_t>(std::clamp(lower, 0.0F, static_cast<float>(inputSize - 1)));
    const auto second =
        static_cast<size_t>(std::clamp(lower + 1, 0.0F, static_cast<float>(inputSize - 1)));
    return {first,
            second,
            static_cast<int>(std::nearbyint((1.0F - weight) * WeightScale)),
            static_cast<int>(std::nearbyint(weight * WeightScale))};
}
} // namespace

Result<std::vector<uint8_t>> resizeBilinear(const std::span<const uint8_t> pixels,
                                            const size_t width,
                                            const size_t height,
                                            const size_t stride,
                                            const size_t channels,
                                            const size_t outputWidth,
                                            const size_t outputHeight) {
    if (!validImage(pixels.size(), width, height, stride, channels, outputWidth, outputHeight)) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData, "Invalid uint8 resize dimensions or backing"));
    }
    std::vector<uint8_t> output(outputWidth * outputHeight * channels);
    std::vector<IntegerSample> horizontal(outputWidth);
    for (size_t x = 0; x < outputWidth; ++x) {
        horizontal[x] = integerSample(x, width, outputWidth);
    }
    for (size_t y = 0; y < outputHeight; ++y) {
        const auto sy = integerSample(y, height, outputHeight);
        for (size_t x = 0; x < outputWidth; ++x) {
            const auto &sx = horizontal[x];
            for (size_t c = 0; c < channels; ++c) {
                const int upper =
                    pixels[sy.first * stride + sx.first * channels + c] * sx.firstWeight +
                    pixels[sy.first * stride + sx.second * channels + c] * sx.secondWeight;
                const int lower =
                    pixels[sy.second * stride + sx.first * channels + c] * sx.firstWeight +
                    pixels[sy.second * stride + sx.second * channels + c] * sx.secondWeight;
                // Retain two fractional output bits, rounding only after the vertical pass.
                const int top = (upper / (1 << HorizontalDiscardBits) * sy.firstWeight) /
                                (1 << VerticalDiscardBits);
                const int bottom = (lower / (1 << HorizontalDiscardBits) * sy.secondWeight) /
                                   (1 << VerticalDiscardBits);
                output[(y * outputWidth + x) * channels + c] =
                    static_cast<uint8_t>((top + bottom + OutputScale / 2) / OutputScale);
            }
        }
    }
    return output;
}

Result<std::vector<float>> resizeBilinear(const std::span<const float> pixels,
                                          const size_t width,
                                          const size_t height,
                                          const size_t outputWidth,
                                          const size_t outputHeight) {
    if (!validImage(pixels.size(), width, height, width, 1, outputWidth, outputHeight)) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData, "Invalid float resize dimensions or backing"));
    }
    std::vector<float> output(outputWidth * outputHeight);
    std::vector<LinearImageSample> horizontal(outputWidth);
    for (size_t x = 0; x < outputWidth; ++x) {
        horizontal[x] = linearImageSample(x, width, outputWidth);
    }
    for (size_t y = 0; y < outputHeight; ++y) {
        const auto sy = linearImageSample(y, height, outputHeight);
        for (size_t x = 0; x < outputWidth; ++x) {
            output[y * outputWidth + x] = interpolateImage(pixels, width, horizontal[x], sy);
        }
    }
    return output;
}
} // namespace opk
