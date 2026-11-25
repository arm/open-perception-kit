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

#include "opk/BilinearImageTensor.h"
#include "opk/ImageResize.h"
#include <array>
#include <cmath>
#include <limits>

namespace opk {
namespace {
constexpr size_t RgbChannels = 3;
constexpr size_t BgraChannels = 4;
constexpr float ByteMaximum = std::numeric_limits<uint8_t>::max();

// Keep the arithmetic kernels out of line: GCC loses loop vectorization when inlined here.
template <size_t SourceChannels, bool Hwc>
[[gnu::noinline]] void packRows(float *const output,
                                const uint8_t *const source,
                                const size_t width,
                                const size_t height,
                                const size_t canvasWidth,
                                const size_t left,
                                const size_t top,
                                const size_t sourceChannel,
                                const float mean,
                                const float deviation) {
    constexpr size_t DestinationStride = Hwc ? RgbChannels : 1;
    for (size_t y = 0; y < height; ++y) {
        float *const row = output + ((y + top) * canvasWidth + left) * DestinationStride;
        const uint8_t *const samples = source + y * width * SourceChannels + sourceChannel;
        for (size_t x = 0; x < width; ++x) {
            const float unit = samples[x * SourceChannels] / ByteMaximum;
            row[x * DestinationStride] = (unit - mean) / deviation;
        }
    }
}

template <size_t Stride>
[[gnu::noinline]] void fillRange(float *const output, const size_t count, const float value) {
    for (size_t x = 0; x < count; ++x) {
        output[x * Stride] = value;
    }
}

template <bool Hwc>
void fillBorders(float *const output,
                 const size_t canvasWidth,
                 const size_t canvasHeight,
                 const size_t left,
                 const size_t top,
                 const size_t width,
                 const size_t height,
                 const float value) {
    constexpr size_t Stride = Hwc ? RgbChannels : 1;
    fillRange<Stride>(output, top * canvasWidth, value);
    // Avoid forming a pointer beyond the allocation for an empty interleaved border.
    if (top + height < canvasHeight) {
        fillRange<Stride>(output + (top + height) * canvasWidth * Stride,
                          (canvasHeight - top - height) * canvasWidth,
                          value);
    }
    for (size_t y = top; y < top + height; ++y) {
        float *const row = output + y * canvasWidth * Stride;
        fillRange<Stride>(row, left, value);
        if (left + width < canvasWidth) {
            fillRange<Stride>(row + (left + width) * Stride, canvasWidth - left - width, value);
        }
    }
}
// Python round() uses ties-to-even; preserve the qualified fixed-canvas geometry.
size_t roundEven(const double value) {
    const double lower = std::floor(value);
    const auto integer = static_cast<size_t>(lower);
    return integer + (value - lower > 0.5 || (value - lower == 0.5 && integer % 2 != 0));
}
} // namespace

Result<BilinearImageTensor> buildBilinearImageTensor(const std::span<const uint8_t> pixels,
                                                     const size_t width,
                                                     const size_t height,
                                                     const size_t stride,
                                                     const RawImagePixelFormat format,
                                                     const ModelInput &input) {
    const bool bgra = format == RawImagePixelFormat::Bgra;
    const size_t channels = bgra ? BgraChannels : RgbChannels;
    const bool hwc = input.dataKind == DataKind::ImageRgbHwc;
    const bool bgr = input.dataKind == DataKind::ImageBgrChw;
    size_t canvasWidth = 0, canvasHeight = 0;
    const size_t limit = std::numeric_limits<size_t>::max();
    const bool supportedSourceFormat = bgra || format == RawImagePixelFormat::Rgb;
    const bool validSourceDimensions =
        width > 0 && height > 0 && width <= limit / channels && stride >= width * channels;
    // The dimension checks guarantee a nonzero stride and a representable row size.
    const bool validSourceStorage = validSourceDimensions &&
                                    height - 1 <= (limit - width * channels) / stride &&
                                    pixels.size() >= (height - 1) * stride + width * channels;

    const bool supportedTensorLayout = input.valueType == Dtype::Float32 &&
                                       (hwc || bgr || input.dataKind == DataKind::ImageRgbChw) &&
                                       input.shape.rank == 4 && input.shape.dims[0] == 1 &&
                                       input.shape.dims[hwc ? 3 : 1] == RgbChannels;
    const bool validCanvasStorage =
        supportedTensorLayout && input.tryGetImageTensorSize(canvasWidth, canvasHeight) &&
        canvasWidth > 0 && canvasHeight > 0 &&
        canvasWidth <= limit / RgbChannels / sizeof(float) / canvasHeight;
    const bool fixedLetterboxInput = input.keepAspectRatio && input.valueInputs.empty() &&
                                     input.matchShapeOutputIndex == InvalidTensorIndex;

    if (!supportedSourceFormat || !validSourceStorage || !validCanvasStorage ||
        !fixedLetterboxInput) {
        return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData,
                                        "Bilinear preprocessing requires valid RGB/BGRA bytes and "
                                        "a fixed Float32 RGB/BGR canvas"));
    }
    const std::array means{input.mean.r, input.mean.g, input.mean.b};
    const std::array deviations{input.std.r, input.std.g, input.std.b};
    const std::array padding{input.letterboxRed, input.letterboxGreen, input.letterboxBlue};
    for (size_t c = 0; c < RgbChannels; ++c) {
        if (!std::isfinite(means[c]) || !std::isfinite(deviations[c]) || deviations[c] <= 0 ||
            !std::isfinite(padding[c]) || padding[c] < 0 || padding[c] > 1) {
            return tl::unexpected(
                OPK_ERROR(ErrorFlag::InvalidData, "Invalid bilinear normalization/padding"));
        }
    }
    const double scale = std::min(static_cast<double>(canvasWidth) / width,
                                  static_cast<double>(canvasHeight) / height);
    BilinearImageTensor result;
    result.width = roundEven(width * scale);
    result.height = roundEven(height * scale);
    if (result.width == 0 || result.height == 0) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::InvalidData, "Aspect ratio collapses after resize"));
    }
    result.left = (canvasWidth - result.width) / 2;
    result.top = (canvasHeight - result.height) / 2;
    try {
        const auto resized =
            resizeBilinear(pixels, width, height, stride, channels, result.width, result.height);
        if (!resized) {
            return tl::unexpected(resized.error());
        }
        const size_t plane = canvasWidth * canvasHeight;
        result.pixels.resize(RgbChannels * plane);
        for (size_t c = 0; c < RgbChannels; ++c) {
            const size_t rgbChannel = bgr ? RgbChannels - 1 - c : c;
            const float pad = (padding[rgbChannel] - means[c]) / deviations[c];
            float *const channelOutput = result.pixels.data() + (hwc ? c : c * plane);
            if (hwc) {
                fillBorders<true>(channelOutput,
                                  canvasWidth,
                                  canvasHeight,
                                  result.left,
                                  result.top,
                                  result.width,
                                  result.height,
                                  pad);
            } else {
                fillBorders<false>(channelOutput,
                                   canvasWidth,
                                   canvasHeight,
                                   result.left,
                                   result.top,
                                   result.width,
                                   result.height,
                                   pad);
            }
            const size_t sourceChannel = bgra ? RgbChannels - 1 - rgbChannel : rgbChannel;
            float *const destination = result.pixels.data() + (hwc ? c : c * plane);
            const uint8_t *const samples = resized->data();
            if (bgra) {
                if (hwc) {
                    packRows<BgraChannels, true>(destination,
                                                 samples,
                                                 result.width,
                                                 result.height,
                                                 canvasWidth,
                                                 result.left,
                                                 result.top,
                                                 sourceChannel,
                                                 means[c],
                                                 deviations[c]);
                } else {
                    packRows<BgraChannels, false>(destination,
                                                  samples,
                                                  result.width,
                                                  result.height,
                                                  canvasWidth,
                                                  result.left,
                                                  result.top,
                                                  sourceChannel,
                                                  means[c],
                                                  deviations[c]);
                }
            } else if (hwc) {
                packRows<RgbChannels, true>(destination,
                                            samples,
                                            result.width,
                                            result.height,
                                            canvasWidth,
                                            result.left,
                                            result.top,
                                            sourceChannel,
                                            means[c],
                                            deviations[c]);
            } else {
                packRows<RgbChannels, false>(destination,
                                             samples,
                                             result.width,
                                             result.height,
                                             canvasWidth,
                                             result.left,
                                             result.top,
                                             sourceChannel,
                                             means[c],
                                             deviations[c]);
            }
        }
        return result;
    } catch (const std::exception &error) {
        return tl::unexpected(OPK_ERROR(ErrorFlag::InvalidData, error.what()));
    }
}
} // namespace opk
