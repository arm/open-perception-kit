/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "runtime/FrameResultsPacket.h"

#include <fmt/core.h>

#include <cstdint>
#include <span>
#include <vector>

namespace pek::runtime {
namespace {

const char *producerIdentityStatusName(perception::container::producer_identity_status status) {
    using perception::container::producer_identity_status;

    switch (status) {
    case producer_identity_status::exact_match:
        return "exact_match";
    case producer_identity_status::missing:
        return "missing";
    case producer_identity_status::malformed:
        return "malformed";
    case producer_identity_status::sdk_name_mismatch:
        return "sdk_name_mismatch";
    case producer_identity_status::sdk_version_mismatch:
        return "sdk_version_mismatch";
    case producer_identity_status::schema_set_mismatch:
        return "schema_set_mismatch";
    }

    return "unknown";
}

} // namespace

Result<perception::container::envelope>
decodeFrameResultsPacket(std::span<const std::uint8_t> packet) {
    perception::container::envelope envelope(packet);
    if (!envelope.valid()) {
        return tl::make_unexpected(
            Error(ErrorFlag::InvalidPipeline,
                  fmt::format("Invalid Perception packet: {}", envelope.error())));
    }

    const auto producerIdentity = envelope.producer_identity();
    if (producerIdentity != perception::container::producer_identity_status::exact_match) {
        return tl::make_unexpected(Error(ErrorFlag::InvalidPipeline,
                                         fmt::format("Unsupported Perception producer identity: {} "
                                                     "(producer={}, version={}, schema={})",
                                                     producerIdentityStatusName(producerIdentity),
                                                     envelope.producer_sdk_name(),
                                                     envelope.producer_sdk_version(),
                                                     envelope.producer_schema_set_sha256())));
    }

    return envelope;
}

Result<perception::container::envelope>
decodeFrameResultsPacket(const std::vector<std::uint8_t> &packet) {
    return decodeFrameResultsPacket(std::span<const std::uint8_t>(packet.data(), packet.size()));
}

} // namespace pek::runtime
