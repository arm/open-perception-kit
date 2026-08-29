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

namespace pek::runtime {

/**
 * @brief Decodes and validates serialized Perception FrameResults packet bytes.
 *
 * The function validates the Perception envelope and requires exact producer
 * identity compatibility before returning the decoded generated envelope.
 * The input bytes are borrowed only during this call. The returned envelope owns
 * its decoded payload storage and does not keep references into packet.
 *
 * @param packet Serialized Perception FrameResults packet bytes.
 * @return Decoded generated Perception envelope, or a runtime Error if the
 *         packet is invalid or was produced by an incompatible SDK/schema.
 */
Result<perception::container::envelope>
decodeFrameResultsPacket(std::span<const std::uint8_t> packet);

/**
 * @brief Decodes and validates serialized Perception FrameResults packet bytes.
 *
 * Convenience overload for callers that own packet bytes in a vector.
 * The vector is borrowed only during this call. The returned envelope owns its
 * decoded payload storage and does not keep references into packet.
 *
 * @param packet Serialized Perception FrameResults packet bytes.
 * @return Decoded generated Perception envelope, or a runtime Error if the
 *         packet is invalid or incompatible.
 */
Result<perception::container::envelope>
decodeFrameResultsPacket(const std::vector<std::uint8_t> &packet);

/**
 * @brief Visits every payload of a generated Perception type in a decoded envelope.
 *
 * This helper keeps application examples on the runtime namespace while still
 * using generated Perception SDK payload types for semantic result access.
 * Payload references passed to visitor are owned by frameResults; copy any data
 * that must outlive the envelope.
 *
 * @param frameResults A decoded and validated generated Perception envelope.
 * @param visitor Callable invoked once for every payload of Payload type.
 */
template <typename Payload, typename Fn>
void visitFrameResultsPayloads(const perception::container::envelope &frameResults, Fn &&visitor) {
    frameResults.for_each<Payload>(std::forward<Fn>(visitor));
}

/**
 * @brief Decodes a packet and visits every payload of a generated Perception type.
 *
 * This is convenient for simple callers. Code that visits multiple payload
 * types should prefer decodeFrameResultsPacket() once, then call
 * visitFrameResultsPayloads() for each type to avoid repeated decoding.
 * Payload references passed to visitor are owned by the temporary decoded
 * envelope and must not be retained after this function returns.
 *
 * @param packet Serialized Perception FrameResults packet bytes.
 * @param visitor Callable invoked once for every payload of Payload type.
 * @return Success, or a runtime Error if packet validation fails.
 */
template <typename Payload, typename Fn>
Result<void> visitFrameResultsPacketPayloads(std::span<const std::uint8_t> packet, Fn &&visitor) {
    auto frameResults = decodeFrameResultsPacket(packet);
    if (!frameResults) {
        return tl::make_unexpected(std::move(frameResults.error()));
    }

    visitFrameResultsPayloads<Payload>(*frameResults, std::forward<Fn>(visitor));
    return {};
}

/**
 * @brief Decodes a packet vector and visits every payload of a generated Perception type.
 *
 * Convenience overload for callers that own packet bytes in a vector.
 * Payload references passed to visitor are owned by the temporary decoded
 * envelope and must not be retained after this function returns.
 *
 * @param packet Serialized Perception FrameResults packet bytes.
 * @param visitor Callable invoked once for every payload of Payload type.
 * @return Success, or a runtime Error if packet validation fails.
 */
template <typename Payload, typename Fn>
Result<void> visitFrameResultsPacketPayloads(const std::vector<std::uint8_t> &packet,
                                             Fn &&visitor) {
    return visitFrameResultsPacketPayloads<Payload>(
        std::span<const std::uint8_t>(packet.data(), packet.size()), std::forward<Fn>(visitor));
}

} // namespace pek::runtime
