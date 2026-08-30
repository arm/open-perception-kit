/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "runtime/Result.h"

#include "perception.h"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

class PerceptionPacket {
  public:
    static pek::runtime::Result<perception::container::envelope>
    decodeFrameResultsPacket(std::span<const std::uint8_t> packet);

    static pek::runtime::Result<perception::container::envelope>
    decodeFrameResultsPacket(const std::vector<std::uint8_t> &packet);

    template <typename Payload, typename Fn>
    static void visitFrameResultsPayloads(const perception::container::envelope &frameResults,
                                          Fn &&visitor) {
        frameResults.for_each<Payload>(std::forward<Fn>(visitor));
    }

    template <typename Payload, typename Fn>
    static pek::runtime::Result<void>
    visitFrameResultsPacketPayloads(std::span<const std::uint8_t> packet, Fn &&visitor) {
        auto frameResults = decodeFrameResultsPacket(packet);
        if (!frameResults) {
            return tl::make_unexpected(std::move(frameResults.error()));
        }

        visitFrameResultsPayloads<Payload>(*frameResults, std::forward<Fn>(visitor));
        return {};
    }

    template <typename Payload, typename Fn>
    static pek::runtime::Result<void>
    visitFrameResultsPacketPayloads(const std::vector<std::uint8_t> &packet, Fn &&visitor) {
        return visitFrameResultsPacketPayloads<Payload>(
            std::span<const std::uint8_t>(packet.data(), packet.size()), std::forward<Fn>(visitor));
    }
};
