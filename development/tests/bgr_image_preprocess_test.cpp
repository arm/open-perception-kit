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
#include <numeric>
#include <string_view>
#include <vector>

#include "preproc/GenericImageTensorBuilder.h"

namespace {

TEST(BgrImagePreprocess, PreservesBgrNormalizationAcrossImagePaths) {
    for (const auto format : {opk::RawImagePixelFormat::Bgra, opk::RawImagePixelFormat::Rgb}) {
        for (const auto mode : {"full", "crop-resize", "letterbox", "destination-rect"}) {
            SCOPED_TRACE(mode);
            SCOPED_TRACE(static_cast<int>(format));
            const bool crop = std::string_view(mode) == "crop-resize";
            const bool letterbox = std::string_view(mode) == "letterbox";
            const bool subrect = std::string_view(mode) == "destination-rect";
            std::vector<uint8_t> pixels =
                format == opk::RawImagePixelFormat::Bgra
                    ? std::vector<uint8_t>{10, 20, 30, 255, 40, 50, 60, 255, 0, 0, 0, 0}
                    : std::vector<uint8_t>{30, 20, 10, 60, 50, 40, 0, 0, 0, 0, 0, 0};
            const size_t width = subrect ? 4 : 2;
            const size_t height = letterbox || subrect ? 3 : 1;
            std::vector<float> output(3 * width * height);
            std::iota(output.begin(), output.end(), -1234.0f);
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
            destination.surfaceWidth = width;
            destination.surfaceHeight = height;
            destination.rect =
                subrect ? opk::PixelRect{1, 1, 2, 1} : opk::PixelRect{0, 0, width, height};
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
                    for (size_t x = 0; x < width; ++x) {
                        const size_t index = channel * width * height + y * width + x;
                        const auto actual = output[index];
                        if (subrect && (x == 0 || x == 3 || y != 1)) {
                            EXPECT_FLOAT_EQ(actual, -1234.0f + static_cast<float>(index));
                        } else {
                            const size_t pixel = subrect ? x - 1 : x;
                            EXPECT_NEAR(actual,
                                        letterbox && y != 1
                                            ? border[channel]
                                            : expected[channel * 2 + (crop ? 1 : pixel)],
                                        1e-4f);
                        }
                    }
        }
    }
}

TEST(BgrImagePreprocess, ConvertsYuvToNormalizedBgr) {
    const std::array<uint8_t, 4> luma = {128, 128, 128, 128};
    const std::array<uint8_t, 2> chroma = {160, 192};
    const std::array<uint8_t, 8> packed = {128, 160, 128, 192, 128, 160, 128, 192};
    for (const auto format : {opk::RawImagePixelFormat::Nv12,
                              opk::RawImagePixelFormat::I420,
                              opk::RawImagePixelFormat::Yuy2}) {
        SCOPED_TRACE(static_cast<int>(format));
        opk::ImageOpDesc source{};
        source.surfaceWidth = 2;
        source.surfaceHeight = 2;
        source.rect = {0, 0, 2, 2};
        source.planeCount = 2;
        source.planes[0] = {luma.data(), nullptr, luma.size(), 2};
        source.planes[1] = {chroma.data(), nullptr, chroma.size(), 2};
        source.format = format;
        if (format == opk::RawImagePixelFormat::I420) {
            source.planeCount = 3;
            source.planes[1] = {chroma.data(), nullptr, 1, 1};
            source.planes[2] = {chroma.data() + 1, nullptr, 1, 1};
        } else if (format == opk::RawImagePixelFormat::Yuy2) {
            source.planeCount = 1;
            source.planes[0] = {packed.data(), nullptr, packed.size(), 4};
            source.planes[1] = {};
        }
        source.type = opk::Dtype::Uint8;
        source.yuvMatrix = opk::YuvColorMatrix::Bt601;
        source.yuvRange = opk::YuvRange::Full;
        source.mean = {104.0f / 255, 117.0f / 255, 123.0f / 255, 0};
        source.std = {2.0f / 255, 4.0f / 255, 3.0f / 255, 1};
        for (const bool subrect : {false, true}) {
            SCOPED_TRACE(subrect);
            const size_t side = subrect ? 4 : 2;
            const size_t offset = subrect ? 1 : 0;
            std::vector<float> output(3 * side * side);
            std::iota(output.begin(), output.end(), -1234.0f);
            opk::ImageOpDesc destination{};
            destination.surfaceWidth = side;
            destination.surfaceHeight = side;
            destination.rect = {offset, offset, 2, 2};
            destination.planeCount = 1;
            destination.planes[0] = {nullptr,
                                     reinterpret_cast<uint8_t *>(output.data()),
                                     output.size() * sizeof(float),
                                     0};
            destination.kind = opk::DataKind::ImageBgrChw;
            destination.type = opk::Dtype::Float32;
            opk::stdop::preproc::GenericImageTensorBuilder builder;
            ASSERT_TRUE(builder.build({source, destination}));
            // BT.601 full-range conversion, followed by channel-specific mean/std.
            const std::array<float, 3> expected = {40.352f, -11.429264f, 31.576f};
            for (size_t channel = 0; channel < 3; ++channel)
                for (size_t y = 0; y < side; ++y)
                    for (size_t x = 0; x < side; ++x) {
                        const size_t index = channel * side * side + y * side + x;
                        const auto actual = output[index];
                        if (subrect && (x == 0 || x == 3 || y == 0 || y == 3))
                            EXPECT_FLOAT_EQ(actual, -1234.0f + static_cast<float>(index));
                        else
                            EXPECT_NEAR(actual, expected[channel], 1e-4f);
                    }
            destination.rect.x = side;
            EXPECT_FALSE(builder.build({source, destination}));
        }
    }
}

} // namespace
