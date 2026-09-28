/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 *************************************************************/

#include "PerceptionPacket.h"

#include <fmt/core.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

const char *
producerIdentityStatusName(open_perception_kit::container::producer_identity_status status) {
    using open_perception_kit::container::producer_identity_status;

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

template <typename Payload>
bool validatePayloadBlobAsType(const open_perception_kit::internalfb::WirePayload &payload,
                               std::size_t payloadIndex,
                               std::string &error) {
    using native_type = std::remove_cvref_t<Payload>;
    using traits = open_perception_kit::detail::native_traits<native_type>;

    if (payload.id() != traits::id) {
        return false;
    }

    const auto *blob = payload.blob();
    if (!blob || blob->size() == 0) {
        error = fmt::format("Invalid Perception payload {} at index {}: empty {} blob",
                            payload.id(),
                            payloadIndex,
                            traits::qualified_root_type);
        return true;
    }

    flatbuffers::Verifier verifier(blob->Data(), blob->size());
    if (!verifier.VerifyBuffer<typename traits::table_type>(traits::file_identifier())) {
        error = fmt::format("Invalid Perception payload {} at index {}: malformed {} blob",
                            payload.id(),
                            payloadIndex,
                            traits::qualified_root_type);
    }
    return true;
}

bool validateKnownPayloadBlob(const open_perception_kit::internalfb::WirePayload &payload,
                              std::size_t payloadIndex,
                              std::string &error) {
    return validatePayloadBlobAsType<open_perception_kit::metadata::BoxDetectionsT>(
               payload, payloadIndex, error) ||
           validatePayloadBlobAsType<open_perception_kit::metadata::ClassificationsT>(
               payload, payloadIndex, error) ||
           validatePayloadBlobAsType<open_perception_kit::metadata::FrameContextT>(
               payload, payloadIndex, error) ||
           validatePayloadBlobAsType<open_perception_kit::metadata::ObjectEmbeddingsT>(
               payload, payloadIndex, error) ||
           validatePayloadBlobAsType<open_perception_kit::metadata::ObjectTracksT>(
               payload, payloadIndex, error) ||
           validatePayloadBlobAsType<open_perception_kit::metadata::PerformanceOverlayT>(
               payload, payloadIndex, error) ||
           validatePayloadBlobAsType<open_perception_kit::metadata::PoseEstimationsT>(
               payload, payloadIndex, error) ||
           validatePayloadBlobAsType<open_perception_kit::metadata::SegmentationMasksT>(
               payload, payloadIndex, error) ||
           validatePayloadBlobAsType<open_perception_kit::metadata::TrackTracesT>(
               payload, payloadIndex, error);
}

opk::runtime::Result<void> validateKnownPayloadBlobs(std::span<const std::uint8_t> packet) {
    const auto *envelope =
        flatbuffers::GetRoot<open_perception_kit::internalfb::WireEnvelope>(packet.data());
    if (!envelope) {
        return {};
    }

    const auto *payloads = envelope->payloads();
    if (!payloads) {
        return {};
    }

    std::size_t payloadIndex = 0;
    for (const auto *payload : *payloads) {
        if (!payload) {
            ++payloadIndex;
            continue;
        }

        std::string error;
        validateKnownPayloadBlob(*payload, payloadIndex, error);
        if (!error.empty()) {
            return tl::unexpected(
                opk::runtime::Error(opk::runtime::ErrorFlag::InvalidPipeline, std::move(error)));
        }
        ++payloadIndex;
    }

    return {};
}

} // namespace

opk::runtime::Result<open_perception_kit::container::envelope>
PerceptionPacket::decodeFrameResultsPacket(std::span<const std::uint8_t> packet) {
    open_perception_kit::container::envelope envelope(packet);
    if (!envelope.valid()) {
        return tl::unexpected(
            opk::runtime::Error(opk::runtime::ErrorFlag::InvalidPipeline,
                                fmt::format("Invalid Perception packet: {}", envelope.error())));
    }

    const auto producerIdentity = envelope.producer_identity();
    if (producerIdentity != open_perception_kit::container::producer_identity_status::exact_match) {
        return tl::unexpected(
            opk::runtime::Error(opk::runtime::ErrorFlag::InvalidPipeline,
                                fmt::format("Unsupported Perception producer identity: {} "
                                            "(producer={}, version={}, schema={})",
                                            producerIdentityStatusName(producerIdentity),
                                            envelope.producer_sdk_name(),
                                            envelope.producer_sdk_version(),
                                            envelope.producer_schema_set_sha256())));
    }

    auto payloadsValid = validateKnownPayloadBlobs(packet);
    if (!payloadsValid) {
        return tl::unexpected(std::move(payloadsValid.error()));
    }

    return envelope;
}

opk::runtime::Result<open_perception_kit::container::envelope>
PerceptionPacket::decodeFrameResultsPacket(const std::vector<std::uint8_t> &packet) {
    return decodeFrameResultsPacket(std::span<const std::uint8_t>(packet.data(), packet.size()));
}
