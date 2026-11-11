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

#include <gtest/gtest.h>

#include <array>
#include <string_view>
#include <vector>

#include "preproc/GenericImageTensorBuilder.h"

namespace {

TEST(BgrImagePreprocess, PreservesBgrNormalizationAcrossImagePaths) {
    for (const auto format : {opk::RawImagePixelFormat::Bgra, opk::RawImagePixelFormat::Rgb}) {
        for (const auto mode : {"full", "crop-resize", "letterbox"}) {
            SCOPED_TRACE(mode);
            SCOPED_TRACE(static_cast<int>(format));
            const bool crop = std::string_view(mode) == "crop-resize";
            const bool letterbox = std::string_view(mode) == "letterbox";
            std::vector<uint8_t> pixels =
                format == opk::RawImagePixelFormat::Bgra
                    ? std::vector<uint8_t>{10, 20, 30, 255, 40, 50, 60, 255, 0, 0, 0, 0}
                    : std::vector<uint8_t>{30, 20, 10, 60, 50, 40, 0, 0, 0, 0, 0, 0};
            const size_t height = letterbox ? 3 : 1;
            std::vector<float> output(3 * 2 * height);
            opk::ImageOpDesc source{};
            source.surfaceWidth = 2;
            source.surfaceHeight = 1;
            source.rect = crop ? opk::PixelRect{1, 0, 1, 1} : opk::PixelRect{0, 0, 2, 1};
            source.planeCount = 1;
            source.planes[0] = {pixels.data(), nullptr, pixels.size(), 12};
            source.format = format;
            source.type = opk::Dtype::Uint8;
            source.mean = {104.0f / 255, 117.0f / 255, 123.0f / 255, 0};
            source.std = {2.0f / 255, 4.0f / 255, 3.0f / 255, 1};
            opk::ImageOpDesc destination{};
            destination.surfaceWidth = 2;
            destination.surfaceHeight = height;
            destination.rect = {0, 0, 2, height};
            destination.keepAspectRatio = letterbox;
            destination.letterboxRed = 90.0f / 255;
            destination.letterboxGreen = 70.0f / 255;
            destination.letterboxBlue = 80.0f / 255;
            destination.planeCount = 1;
            destination.planes[0] = {nullptr,
                                     reinterpret_cast<uint8_t *>(output.data()),
                                     output.size() * sizeof(float),
                                     0};
            destination.kind = opk::DataKind::ImageBgrChw;
            destination.type = opk::Dtype::Float32;
            opk::stdop::preproc::GenericImageTensorBuilder builder;
            ASSERT_TRUE(builder.build({source, destination}));
            const std::array<float, 6> expected = {-47, -32, -24.25f, -16.75f, -31, -21};
            const std::array<float, 3> border = {-12, -11.75f, -11};
            for (size_t channel = 0; channel < 3; ++channel)
                for (size_t y = 0; y < height; ++y)
                    for (size_t x = 0; x < 2; ++x)
                        EXPECT_NEAR(output[channel * 2 * height + y * 2 + x],
                                    letterbox && y != 1 ? border[channel]
                                                        : expected[channel * 2 + (crop ? 1 : x)],
                                    1e-4f);
        }
    }
}

TEST(BgrImagePreprocess, ConvertsNv12ToNormalizedBgr) {
    const std::array<uint8_t, 4> luma = {128, 128, 128, 128};
    const std::array<uint8_t, 2> chroma = {160, 192};
    std::array<float, 12> output{};
    opk::ImageOpDesc source{};
    source.surfaceWidth = 2;
    source.surfaceHeight = 2;
    source.rect = {0, 0, 2, 2};
    source.planeCount = 2;
    source.planes[0] = {luma.data(), nullptr, luma.size(), 2};
    source.planes[1] = {chroma.data(), nullptr, chroma.size(), 2};
    source.format = opk::RawImagePixelFormat::Nv12;
    source.type = opk::Dtype::Uint8;
    source.yuvMatrix = opk::YuvColorMatrix::Bt601;
    source.yuvRange = opk::YuvRange::Full;
    source.mean = {104.0f / 255, 117.0f / 255, 123.0f / 255, 0};
    source.std = {2.0f / 255, 4.0f / 255, 3.0f / 255, 1};
    opk::ImageOpDesc destination{};
    destination.surfaceWidth = 2;
    destination.surfaceHeight = 2;
    destination.rect = {0, 0, 2, 2};
    destination.planeCount = 1;
    destination.planes[0] = {
        nullptr, reinterpret_cast<uint8_t *>(output.data()), sizeof(output), 0};
    destination.kind = opk::DataKind::ImageBgrChw;
    destination.type = opk::Dtype::Float32;
    opk::stdop::preproc::GenericImageTensorBuilder builder;
    ASSERT_TRUE(builder.build({source, destination}));
    // BT.601 full-range conversion, followed by channel-specific mean/std.
    const std::array<float, 3> expected = {40.352f, -11.429264f, 31.576f};
    for (size_t channel = 0; channel < 3; ++channel)
        for (size_t pixel = 0; pixel < 4; ++pixel)
            EXPECT_NEAR(output[channel * 4 + pixel], expected[channel], 1e-4f);
}

} // namespace
