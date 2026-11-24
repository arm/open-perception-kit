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

#include "frame_reply_generated.h"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>

namespace opk::frame_access {

struct DeploymentProfile {
    uint64_t clients;
    uint64_t activeRequestsPerClient;
    uint64_t callbackAttempts;
    uint64_t maxImageBytes;
    uint64_t maxResultsBytes;
    uint64_t maxConstructionBytes;
};

struct ConstructionBounds {
    std::array<uint64_t, 3> planeBytes;
    uint64_t planeCount;
    uint64_t imageBytes;
    uint64_t constructionBytes;
    uint64_t responseSlots;
    uint64_t responsePayloadCapacity;
};

inline std::optional<ConstructionBounds> constructionBounds(const DeploymentProfile &profile,
                                                            const v1::PixelFormat format,
                                                            const uint32_t width,
                                                            const uint32_t height) {
    constexpr uint64_t flatbufferLimit = (uint64_t{1} << 31) - 1;
    if (width == 0 || height == 0 || profile.clients == 0 || profile.activeRequestsPerClient == 0 ||
        profile.callbackAttempts == 0 || profile.maxImageBytes == 0 ||
        profile.maxResultsBytes == 0 || profile.maxConstructionBytes > flatbufferLimit) {
        return std::nullopt;
    }
    const auto w = uint64_t{width};
    const auto h = uint64_t{height};
    const auto cw = (w + 1) / 2;
    const auto ch = (h + 1) / 2;
    if (w * h > flatbufferLimit) {
        return std::nullopt;
    }
    ConstructionBounds result{};
    result.planeCount = 1;
    switch (format) {
    case v1::PixelFormat_BGRA8:
        result.planeBytes[0] = 4 * w * h;
        break;
    case v1::PixelFormat_RGB8:
        result.planeBytes[0] = 3 * w * h;
        break;
    case v1::PixelFormat_Gray8:
        result.planeBytes[0] = w * h;
        break;
    case v1::PixelFormat_I420:
        result.planeBytes = {w * h, cw * ch, cw * ch};
        result.planeCount = 3;
        break;
    case v1::PixelFormat_NV12:
        result.planeBytes = {w * h, 2 * cw * ch, 0};
        result.planeCount = 2;
        break;
    case v1::PixelFormat_YUY2:
        result.planeBytes[0] = 4 * cw * h;
        break;
    default:
        return std::nullopt;
    }
    result.imageBytes = result.planeBytes[0] + result.planeBytes[1] + result.planeBytes[2];
    // Includes vtables, alignment and builder scratch; derivation: tools/frame-access/README.md.
    constexpr uint64_t outerConstructionOverhead = 1024;
    if (result.imageBytes > profile.maxImageBytes || result.imageBytes > flatbufferLimit ||
        profile.maxResultsBytes > flatbufferLimit - outerConstructionOverhead ||
        result.imageBytes > flatbufferLimit - outerConstructionOverhead - profile.maxResultsBytes) {
        return std::nullopt;
    }
    result.constructionBytes =
        (result.imageBytes + profile.maxResultsBytes + outerConstructionOverhead + 7) &
        ~uint64_t{7};
    if (result.constructionBytes > profile.maxConstructionBytes ||
        profile.clients > std::numeric_limits<uint64_t>::max() / 6 ||
        profile.activeRequestsPerClient >
            std::numeric_limits<uint64_t>::max() / (6 * profile.clients)) {
        return std::nullopt;
    }
    result.responseSlots = 6 * profile.clients * profile.activeRequestsPerClient;
    if (result.responseSlots > std::numeric_limits<uint64_t>::max() / result.constructionBytes) {
        return std::nullopt;
    }
    result.responsePayloadCapacity = result.responseSlots * result.constructionBytes;
    return result;
}

} // namespace opk::frame_access
