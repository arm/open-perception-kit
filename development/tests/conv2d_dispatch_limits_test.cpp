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

#include "../examples/mlwl-gpu/conv2d_dispatch_limits.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <vector>

using opk::rpi5::conv2dDispatchLimits;

TEST(Conv2DDispatchLimits, CapsWorkAcrossAllThreeAxes) {
    EXPECT_EQ(conv2dDispatchLimits({65535, 65535, 65535}, 3, 3, 128),
              (std::array<uint32_t, 3>{1, 1, 16}));
    EXPECT_EQ(conv2dDispatchLimits({65535, 65535, 65535}, 1, 1, 384),
              (std::array<uint32_t, 3>{1, 1, 48}));
    EXPECT_EQ(conv2dDispatchLimits({20, 10, 2}, 3, 3, 128), (std::array<uint32_t, 3>{1, 8, 2}));
    EXPECT_EQ(conv2dDispatchLimits({20, 2, 2}, 3, 3, 128), (std::array<uint32_t, 3>{4, 2, 2}));
    EXPECT_EQ(conv2dDispatchLimits({2, 2, 2}, 3, 3, 128), (std::array<uint32_t, 3>{2, 2, 2}));
}

TEST(Conv2DDispatchLimits, LargeDimensionsCannotOverflowOrProduceZeroSteps) {
    constexpr auto largest = std::numeric_limits<uint64_t>::max();
    EXPECT_EQ(conv2dDispatchLimits({65535, 65535, 65535}, largest, largest, largest),
              (std::array<uint32_t, 3>{1, 1, 1}));
    EXPECT_EQ(conv2dDispatchLimits({65535, 65535, 65535}, 1, 1, 18433),
              (std::array<uint32_t, 3>{1, 1, 1}));
    EXPECT_EQ(conv2dDispatchLimits({1, 1, 1}, 1, 1, 1), (std::array<uint32_t, 3>{1, 1, 1}));
}

TEST(Conv2DDispatchLimits, OffsetTilesCoverEveryOutputOnceIncludingTails) {
    constexpr uint32_t batches = 3, height = 17, width = 19, channels = 70;
    constexpr std::array<uint32_t, 3> totalGroups{
        (batches * width + 7) / 8, (height + 7) / 8, (channels + 3) / 4};
    for (const auto deviceLimits :
         {std::array<uint32_t, 3>{65535, 65535, 65535}, std::array<uint32_t, 3>{20, 2, 2}}) {
        const auto limits = conv2dDispatchLimits(deviceLimits, 3, 3, 128);
        std::vector<uint32_t> visits(std::size_t{batches} * height * width * channels);
        for (uint32_t x = 0; x < totalGroups[0]; x += limits[0]) {
            for (uint32_t y = 0; y < totalGroups[1]; y += limits[1]) {
                for (uint32_t z = 0; z < totalGroups[2]; z += limits[2]) {
                    const auto countX = std::min(limits[0], totalGroups[0] - x);
                    const auto countY = std::min(limits[1], totalGroups[1] - y);
                    const auto countZ = std::min(limits[2], totalGroups[2] - z);
                    ASSERT_LE(countX * countY * countZ, 16u);
                    // The existing Graph offset loops and Conv2D shader coordinate mapping.
                    for (uint32_t ix = x * 8; ix < (x + countX) * 8; ++ix) {
                        for (uint32_t iy = y * 8; iy < (y + countY) * 8; ++iy) {
                            for (uint32_t ic = z * 4; ic < (z + countZ) * 4; ++ic) {
                                const auto batch = ix % batches;
                                const auto column = ix / batches;
                                if (column < width && iy < height && ic < channels) {
                                    ++visits[((batch * height + iy) * width + column) * channels +
                                             ic];
                                }
                            }
                        }
                    }
                }
            }
        }
        EXPECT_TRUE(
            std::all_of(visits.begin(), visits.end(), [](const auto count) { return count == 1; }));
    }
}
