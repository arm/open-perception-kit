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

#include <algorithm>
#include <array>
#include <cstdint>

namespace opk::rpi5 {

inline std::array<uint32_t, 3> conv2dDispatchLimits(const std::array<uint32_t, 3> &deviceLimits,
                                                    const uint64_t kernelHeight,
                                                    const uint64_t kernelWidth,
                                                    const uint64_t inputChannels) {
    // Keep measured RPi5 FP32 convolutions away from the 500 ms progress watchdog.
    uint64_t groups = 18432;
    for (const auto dimension : {kernelHeight, kernelWidth, inputChannels}) {
        groups /= std::max(uint64_t{1}, dimension);
    }
    // shortcut: one workgroup is indivisible; recalibrate for other shapes or devices.
    groups = std::max(uint64_t{1}, groups);
    std::array<uint32_t, 3> limits{};
    // Vulkan device limits are positive; spend the budget on contiguous channels first.
    for (int axis = 2; axis >= 0; --axis) {
        limits[axis] = std::min(deviceLimits[axis], static_cast<uint32_t>(groups));
        groups /= limits[axis];
    }
    return limits;
}

} // namespace opk::rpi5
