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
#include <gtest/gtest.h>
#include <limits>

TEST(ImageResize, NearestPreservesExistingFloorMapping) {
    constexpr std::array<size_t, 7> expected{0, 0, 0, 1, 1, 2, 2};
    for (size_t x = 0; x < expected.size(); ++x) {
        EXPECT_EQ(opk::nearestImageSample(x, 3, expected.size()), expected[x]);
    }
    EXPECT_EQ(opk::nearestImageSample(2, 7, 3), 4);
}

TEST(ImageResize, BilinearFloatUsesHalfPixelCentresAndReplicatedEdges) {
    const std::array<float, 4> input{0, 4, 8, 12};
    const auto output = opk::resizeBilinear(input, 2, 2, 4, 4);
    ASSERT_TRUE(output);
    const std::vector<float> expected{0, 1, 3, 4, 2, 3, 5, 6, 6, 7, 9, 10, 8, 9, 11, 12};
    EXPECT_EQ(*output, expected);
    const auto reduced = opk::resizeBilinear(input, 2, 2, 1, 1);
    ASSERT_TRUE(reduced);
    EXPECT_FLOAT_EQ((*reduced)[0], 6);
    const std::array<float, 1> single{-2};
    const auto expanded = opk::resizeBilinear(single, 1, 1, 3, 5);
    ASSERT_TRUE(expanded);
    EXPECT_EQ(*expanded, std::vector<float>(15, -2));
}

TEST(ImageResize, Uint8SupportsPaddedRowsAndPreservesChannels) {
    for (const size_t channels : {1U, 3U, 4U}) {
        const size_t stride = 2 * channels + 5;
        std::vector<uint8_t> input(stride * 2, 255);
        for (size_t y = 0; y < 2; ++y) {
            for (size_t x = 0; x < 2; ++x) {
                for (size_t c = 0; c < channels; ++c) {
                    input[y * stride + x * channels + c] = 8 * y + 4 * x + c;
                }
            }
        }
        const auto output = opk::resizeBilinear(input, 2, 2, stride, channels, 4, 4);
        ASSERT_TRUE(output);
        const std::array<uint8_t, 16> expected{0, 1, 3, 4, 2, 3, 5, 6, 6, 7, 9, 10, 8, 9, 11, 12};
        for (size_t i = 0; i < expected.size(); ++i) {
            for (size_t c = 0; c < channels; ++c) {
                EXPECT_EQ((*output)[i * channels + c], expected[i] + c);
            }
        }
    }
}

TEST(ImageResize, RejectsInvalidGeometryAndBacking) {
    const std::array<uint8_t, 4> pixels{0, 1, 2, 3};
    EXPECT_FALSE(opk::resizeBilinear(pixels, 0, 2, 2, 1, 2, 2));
    EXPECT_FALSE(opk::resizeBilinear(pixels, 2, 2, 1, 1, 2, 2));
    EXPECT_FALSE(opk::resizeBilinear(pixels, 2, 3, 2, 1, 2, 2));
    EXPECT_FALSE(opk::resizeBilinear(pixels, 1, 1, 2, 2, 2, 2));
    EXPECT_FALSE(opk::resizeBilinear(pixels, 2, 2, 2, 1, 0, 2));
    EXPECT_FALSE(opk::resizeBilinear(pixels, 2, 2, 2, 1, std::numeric_limits<size_t>::max(), 2));
    const std::array<float, 1> scalar{0};
    EXPECT_FALSE(opk::resizeBilinear(scalar, 2, 2, 2, 2));
}

TEST(BilinearImageTensor, UsesModelLayoutNormalizationPaddingAndRectangularCanvas) {
    opk::ModelInput input;
    input.shape = opk::Shape(1, 3, 4, 6);
    input.dataKind = opk::DataKind::ImageBgrChw;
    input.keepAspectRatio = true;
    input.mean = {0.1F, 0.2F, 0.3F, 0};
    input.std = {0.5F, 0.25F, 1.0F, 1};
    input.letterboxRed = 0.25F;
    input.letterboxGreen = 0.5F;
    input.letterboxBlue = 0.75F;
    const std::vector<uint8_t> rgb{255, 0, 128};
    const auto output =
        opk::buildBilinearImageTensor(rgb, 1, 1, 3, opk::RawImagePixelFormat::Rgb, input);
    ASSERT_TRUE(output);
    EXPECT_EQ(output->left, 1);
    EXPECT_EQ(output->width, 4);
    EXPECT_FLOAT_EQ(output->pixels[0], (0.75F - 0.1F) / 0.5F);
    EXPECT_FLOAT_EQ(output->pixels[1], (128.0F / 255.0F - 0.1F) / 0.5F);
    EXPECT_FLOAT_EQ(output->pixels[24 + 1], (0.0F - 0.2F) / 0.25F);
    EXPECT_FLOAT_EQ(output->pixels[48 + 1], 1.0F - 0.3F);
    input.dataKind = opk::DataKind::ImageRgbHwc;
    input.shape = opk::Shape(1, 4, 6, 3);
    const auto hwc =
        opk::buildBilinearImageTensor(rgb, 1, 1, 3, opk::RawImagePixelFormat::Rgb, input);
    ASSERT_TRUE(hwc);
    EXPECT_FLOAT_EQ(hwc->pixels[0], (0.25F - 0.1F) / 0.5F);
    EXPECT_FLOAT_EQ(hwc->pixels[3], (1.0F - 0.1F) / 0.5F);
}

TEST(BilinearImageTensor, PreservesTiesToEvenAndRejectsInvalidStorageWithoutArbitrarySideLimit) {
    opk::ModelInput input;
    input.dataKind = opk::DataKind::ImageRgbChw;
    input.shape = opk::Shape(1, 3, 5, 5);
    input.keepAspectRatio = true;
    const std::vector<uint8_t> rgb(4 * 2 * 3, 128);
    const auto output =
        opk::buildBilinearImageTensor(rgb, 4, 2, 12, opk::RawImagePixelFormat::Rgb, input);
    ASSERT_TRUE(output);
    EXPECT_EQ(output->height, 2);
    EXPECT_EQ(output->top, 1);
    EXPECT_FALSE(
        opk::buildBilinearImageTensor(rgb, 4, 2, 11, opk::RawImagePixelFormat::Rgb, input));
    input.shape = opk::Shape(1, 3, 1, 4097);
    EXPECT_TRUE(opk::buildBilinearImageTensor(rgb, 1, 1, 3, opk::RawImagePixelFormat::Rgb, input));
    input.std.r = 0;
    EXPECT_FALSE(opk::buildBilinearImageTensor(rgb, 1, 1, 3, opk::RawImagePixelFormat::Rgb, input));
}

TEST(BilinearImageTensor, PacksAllLayoutsWithHorizontalVerticalAndEmptyBorders) {
    const std::array<float, 3> rgb{1.0F, 128.0F / 255.0F, 0.0F};
    for (const bool bgra : {false, true}) {
        const std::vector<uint8_t> pixels =
            bgra ? std::vector<uint8_t>{0, 128, 255, 77} : std::vector<uint8_t>{255, 128, 0};
        for (const auto layout :
             {opk::DataKind::ImageRgbChw, opk::DataKind::ImageBgrChw, opk::DataKind::ImageRgbHwc}) {
            for (const auto canvas : {std::array{7, 4}, std::array{4, 7}, std::array{4, 4}}) {
                const bool hwc = layout == opk::DataKind::ImageRgbHwc;
                opk::ModelInput input;
                input.dataKind = layout;
                input.keepAspectRatio = true;
                input.shape = hwc ? opk::Shape(1, canvas[1], canvas[0], 3)
                                  : opk::Shape(1, 3, canvas[1], canvas[0]);
                input.mean = {0.1F, 0.2F, 0.3F, 0};
                input.std = {0.5F, 0.25F, 1.0F, 1};
                input.letterboxRed = 0.25F;
                input.letterboxGreen = 0.5F;
                input.letterboxBlue = 0.75F;
                const auto output = opk::buildBilinearImageTensor(
                    pixels,
                    1,
                    1,
                    pixels.size(),
                    bgra ? opk::RawImagePixelFormat::Bgra : opk::RawImagePixelFormat::Rgb,
                    input);
                ASSERT_TRUE(output);
                EXPECT_EQ(output->width, 4);
                EXPECT_EQ(output->height, 4);
                EXPECT_EQ(output->left, static_cast<size_t>((canvas[0] - 4) / 2));
                EXPECT_EQ(output->top, static_cast<size_t>((canvas[1] - 4) / 2));
                const std::array padding{
                    input.letterboxRed, input.letterboxGreen, input.letterboxBlue};
                const std::array means{input.mean.r, input.mean.g, input.mean.b};
                const std::array deviations{input.std.r, input.std.g, input.std.b};
                for (size_t y = 0; y < static_cast<size_t>(canvas[1]); ++y) {
                    for (size_t x = 0; x < static_cast<size_t>(canvas[0]); ++x) {
                        const bool inside = x >= output->left && x < output->left + 4 &&
                                            y >= output->top && y < output->top + 4;
                        for (size_t c = 0; c < 3; ++c) {
                            const size_t channel = layout == opk::DataKind::ImageBgrChw ? 2 - c : c;
                            const float unit = inside ? rgb[channel] : padding[channel];
                            const size_t pixel = y * canvas[0] + x;
                            const size_t index =
                                hwc ? pixel * 3 + c : c * canvas[0] * canvas[1] + pixel;
                            EXPECT_FLOAT_EQ(output->pixels[index],
                                            (unit - means[c]) / deviations[c]);
                        }
                    }
                }
            }
        }
    }
}
