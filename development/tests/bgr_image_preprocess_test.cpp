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
#include <vector>

#include "preproc/GenericImageTensorBuilder.h"

namespace {

TEST(BgrImagePreprocess, PreservesBgrNormalizationWithCropResizeAndPaddedSource) {
    for (const auto format : {opk::RawImagePixelFormat::Bgra, opk::RawImagePixelFormat::Rgb}) {
        std::vector<uint8_t> pixels =
            format == opk::RawImagePixelFormat::Bgra
                ? std::vector<uint8_t>{10, 20, 30, 255, 40, 50, 60, 255, 0, 0, 0, 0}
                : std::vector<uint8_t>{30, 20, 10, 60, 50, 40, 0, 0, 0, 0, 0, 0};
        std::array<float, 6> output{};
        opk::ImageOpDesc source{};
        source.surfaceWidth = 2;
        source.surfaceHeight = 1;
        source.rect = {1, 0, 1, 1};
        source.planeCount = 1;
        source.planes[0] = {pixels.data(), nullptr, pixels.size(), 12};
        source.format = format;
        source.type = opk::Dtype::Uint8;
        source.mean = {104.0f / 255, 117.0f / 255, 123.0f / 255, 0};
        source.std = {2.0f / 255, 4.0f / 255, 3.0f / 255, 1};
        opk::ImageOpDesc destination{};
        destination.surfaceWidth = 2;
        destination.surfaceHeight = 1;
        destination.rect = {0, 0, 2, 1};
        destination.planeCount = 1;
        destination.planes[0] = {
            nullptr, reinterpret_cast<uint8_t *>(output.data()), sizeof(output), 0};
        destination.kind = opk::DataKind::ImageBgrChw;
        destination.type = opk::Dtype::Float32;
        opk::stdop::preproc::GenericImageTensorBuilder builder;
        ASSERT_TRUE(builder.build({source, destination}));
        const std::array<float, 6> expected = {-32, -32, -16.75f, -16.75f, -21, -21};
        for (size_t i = 0; i < expected.size(); ++i)
            EXPECT_NEAR(output[i], expected[i], 1e-4f);
    }
}

} // namespace
