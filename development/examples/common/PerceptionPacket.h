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

#include "runtime/Result.h"

#include "open_perception_kit.h"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

class PerceptionPacket {
  public:
    static opk::runtime::Result<open_perception_kit::container::envelope>
    decodeFrameResultsPacket(std::span<const std::uint8_t> packet);

    static opk::runtime::Result<open_perception_kit::container::envelope>
    decodeFrameResultsPacket(const std::vector<std::uint8_t> &packet);

    template <typename Payload, typename Fn>
    static void
    visitFrameResultsPayloads(const open_perception_kit::container::envelope &frameResults,
                              Fn &&visitor) {
        frameResults.for_each<Payload>(std::forward<Fn>(visitor));
    }

    template <typename Payload, typename Fn>
    static opk::runtime::Result<void>
    visitFrameResultsPacketPayloads(std::span<const std::uint8_t> packet, Fn &&visitor) {
        auto frameResults = decodeFrameResultsPacket(packet);
        if (!frameResults) {
            return tl::unexpected(std::move(frameResults.error()));
        }

        visitFrameResultsPayloads<Payload>(*frameResults, std::forward<Fn>(visitor));
        return {};
    }

    template <typename Payload, typename Fn>
    static opk::runtime::Result<void>
    visitFrameResultsPacketPayloads(const std::vector<std::uint8_t> &packet, Fn &&visitor) {
        return visitFrameResultsPacketPayloads<Payload>(
            std::span<const std::uint8_t>(packet.data(), packet.size()), std::forward<Fn>(visitor));
    }
};
