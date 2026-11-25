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

#include "frame_access_server.h"

#include "iox2/iceoryx2.hpp"

#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

IOX2_DEFINE_TYPE_NAME(opk::frame_access::v1::FrameReply, "FrameReply");

namespace opk::frame_access {

namespace {
constexpr auto ipc = iox2::ServiceType::Ipc;
using Reply = iox2::Flatbuffer<v1::FrameReply>;
using Factory = iox2::PortFactoryRequestResponse<ipc, uint64_t, void, Reply, void>;
using NativeServer = iox2::Server<ipc, uint64_t, void, Reply, void>;
using Request = iox2::ActiveRequest<ipc, uint64_t, void, Reply, void>;
} // namespace

struct Server::Impl {
    DeploymentProfile profile;
    ConstructionBounds bounds;
    uint64_t reservedBytes;
    v1::PixelFormat format;
    uint32_t width;
    uint32_t height;
    ReportError reportError;
    std::optional<iox2::Node<ipc>> node;
    std::optional<Factory> factory;
    std::optional<NativeServer> server;

    Impl(const DeploymentProfile &configuration,
         const v1::PixelFormat pixelFormat,
         const uint32_t w,
         const uint32_t h,
         ReportError report)
        : profile(configuration), format(pixelFormat), width(w), height(h),
          reportError(std::move(report)) {
        const auto calculated = constructionBounds(profile, format, width, height);
        if (!calculated) {
            throw std::invalid_argument("frame-access profile cannot support negotiated layout");
        }
        bounds = *calculated;
        reservedBytes = bounds.constructionBytes;
    }

    flatbuffers::Offset<v1::Frame> packFrame(flatbuffers::FlatBufferBuilder &builder,
                                             const mediaio::VideoFrame &frame) const {
        std::array<flatbuffers::Offset<v1::Plane>, 3> planes{};
        for (size_t i = 0; i < bounds.planeCount; ++i) {
            const auto &source = frame.planes()[i];
            const uint64_t rows = i == 0 ? height : (uint64_t{height} + 1) / 2;
            const auto rowBytes = bounds.planeBytes[i] / rows;
            const auto *data = static_cast<const uint8_t *>(source.data());

            uint8_t *destination = nullptr;
            const auto pixels =
                builder.CreateUninitializedVector(bounds.planeBytes[i], &destination);
            // Fill pixels before another builder allocation can invalidate destination.
            for (uint64_t row = 0; row < rows; ++row) {
                std::memcpy(
                    destination + row * rowBytes, data + row * source.strideBytes(), rowBytes);
                if (format == v1::PixelFormat_YUY2 && width % 2 != 0) {
                    // The final Y1 is outside the image; never export its source byte.
                    destination[(row + 1) * rowBytes - 2] = 0;
                }
            }
            planes[i] = v1::CreatePlane(builder, pixels);
        }

        const auto planeVector = builder.CreateVector(planes.data(), bounds.planeCount);
        return v1::CreateFrame(builder, format, width, height, planeVector);
    }

    void reply(Request &request,
               const v1::ReplyStatus status,
               const uint64_t sequence,
               const std::optional<uint64_t> pts,
               const flatbuffers::DetachedBuffer *results,
               const mediaio::VideoFrame *frame) {
        auto loan = request.loan_flatbuffer();
        if (!loan.has_value()) {
            reportError("frame-access response loan failed; releasing request");
            return;
        }

        // The outer FlatBuffer is built in iceoryx2's response loan, not a staging buffer.
        auto response = std::move(loan.value());
        auto &builder = response.flatbuffer_builder();

        flatbuffers::Offset<v1::FrameReply> root;
        if (status != v1::ReplyStatus_Ok) {
            root = v1::CreateFrameReply(builder, status);
        } else {
            // Preserve the serialized OPK envelope; copy results and packed pixels into this loan.
            const auto resultBytes = builder.CreateVector(results->data(), results->size());
            flatbuffers::Offset<v1::Frame> wireFrame;
            if (frame != nullptr) {
                wireFrame = packFrame(builder, *frame);
            }

            const auto sample = v1::CreateSampleInfo(builder, sequence, pts);
            root =
                v1::CreateFrameReply(builder, v1::ReplyStatus_Ok, sample, resultBytes, wireFrame);
        }

        // Finalize the root and transfer the response loan to the middleware.
        if (!iox2::send(iox2::assume_init(std::move(response), root)).has_value()) {
            reportError("frame-access response send failed; releasing request");
        }
    }

    bool validFrame(const mediaio::VideoFrame &frame) const {
        constexpr std::array formats{RawImagePixelFormat::Unknown,
                                     RawImagePixelFormat::Bgra,
                                     RawImagePixelFormat::Rgb,
                                     RawImagePixelFormat::Gray,
                                     RawImagePixelFormat::I420,
                                     RawImagePixelFormat::Nv12,
                                     RawImagePixelFormat::Yuy2};
        if (frame.width() != width || frame.height() != height ||
            frame.format() != formats[static_cast<size_t>(format)] ||
            frame.planes().size() != bounds.planeCount) {
            return false;
        }
        for (size_t i = 0; i < bounds.planeCount; ++i) {
            const auto &plane = frame.planes()[i];
            const uint64_t rows = i == 0 ? height : (uint64_t{height} + 1) / 2;
            const auto rowBytes = bounds.planeBytes[i] / rows;
            if (!plane.hasHostData() || !plane.canRead() || plane.strideBytes() < rowBytes ||
                plane.byteSize() < (rows - 1) * plane.strideBytes() + rowBytes) {
                return false;
            }
        }
        return true;
    }
};

Server::Server(const DeploymentProfile &profile,
               const std::string_view endpoint,
               const std::string_view runtimePath,
               const std::string_view schemaPath,
               const v1::PixelFormat format,
               const uint32_t width,
               const uint32_t height,
               ReportError reportError)
    : impl(std::make_unique<Impl>(profile, format, width, height, std::move(reportError))) {
    auto config = iox2::Config();
    if (!runtimePath.empty()) {
        const auto path = iox2::bb::Path::create(std::string(runtimePath).c_str());
        if (!path.has_value())
            throw std::invalid_argument("invalid frame-access runtime path");
        config.global().set_root_path(path.value());
    }

    // 0.10.0 adds a separator, prefix, SHA1, two u128 IDs and suffix to connection paths.
    constexpr uint64_t connectionPathOverhead = 1 + 5 + 40 + 1 + 39 + 1 + 39 + 11;
    if (std::string_view(config.global().root_path()).size() >
        iox2::bb::Path::capacity() - connectionPathOverhead) {
        throw std::invalid_argument("runtime path is too long for native connection names");
    }

    const auto name = iox2::ServiceName::create(std::string(endpoint).c_str());
    const auto schema = iox2::bb::FilePath::create(std::string(schemaPath).c_str());
    if (!name.has_value() || !schema.has_value()) {
        throw std::invalid_argument("invalid frame-access endpoint or schema path");
    }

    auto node = iox2::NodeBuilder().config(config).create<ipc>();
    if (!node.has_value())
        throw std::runtime_error("frame-access native node creation failed");
    impl->node.emplace(std::move(node.value()));

    // Native limits provide backpressure; queued requests and replies must not be overwritten.
    auto service = impl->node->service_builder(name.value())
                       .request_response<uint64_t, Reply>()
                       .response_flatbuffer_schema_path(schema.value())
                       .max_servers(1)
                       .max_nodes(profile.clients + 1)
                       .max_clients(profile.clients)
                       .max_active_requests_per_client(profile.activeRequestsPerClient)
                       .max_loaned_requests(profile.activeRequestsPerClient)
                       .max_response_buffer_size(1)
                       .max_borrowed_responses_per_pending_response(1)
                       .enable_safe_overflow_for_requests(false)
                       .enable_safe_overflow_for_responses(false)
                       .enable_fire_and_forget_requests(false)
                       .create();
    if (!service.has_value())
        throw std::runtime_error("frame-access native service creation failed");
    impl->factory.emplace(std::move(service.value()));

    // Reserve the checked construction bound; this does not guarantee OS backing pages.
    auto server = impl->factory->server_builder()
                      .initial_reserved_memory(impl->reservedBytes)
                      .allocation_strategy(iox2::AllocationStrategy::Static)
                      .max_loaned_responses_per_request(1)
                      .create();
    if (!server.has_value())
        throw std::runtime_error("frame-access native server creation failed");
    impl->server.emplace(std::move(server.value()));
}

Server::~Server() = default;

bool Server::setLayout(const v1::PixelFormat format, const uint32_t width, const uint32_t height) {
    const auto bounds = constructionBounds(impl->profile, format, width, height);
    if (!bounds || bounds->constructionBytes > impl->reservedBytes)
        return false;
    impl->bounds = *bounds;
    impl->format = format;
    impl->width = width;
    impl->height = height;
    return true;
}

void Server::serve(const open_perception_kit::container::envelope *results,
                   const uint64_t sequence,
                   const std::optional<uint64_t> pts,
                   const MapFrame &mapFrame) {
    // Reuse work only within this callback; keep no source-frame cache between buffers.
    std::optional<flatbuffers::DetachedBuffer> serialized;
    std::shared_ptr<mediaio::VideoFrame> frame;
    bool serializationAttempted = false;
    bool mappingAttempted = false;
    auto resultStatus = v1::ReplyStatus_NotReady;
    auto frameStatus = v1::ReplyStatus_UnsupportedFrame;

    // Budget counts returned requests, not middleware-discarded cancellations; no time bound.
    for (uint64_t attempt = 0; attempt < impl->profile.callbackAttempts; ++attempt) {
        auto received = impl->server->receive();
        if (!received.has_value()) {
            impl->reportError("frame-access request receive failed");
            break;
        }

        auto pending = std::move(received.value());
        if (!pending.has_value())
            break;
        auto &request = pending.value();
        if (!request.is_connected())
            continue;

        try {
            if (request.payload() > 1) {
                impl->reply(
                    request, v1::ReplyStatus_InvalidRequest, sequence, pts, nullptr, nullptr);
                continue;
            }
            const bool includeFrame = request.payload() == 1;

            // Serialize once per completed buffer, only after receiving a valid request.
            if (!serializationAttempted) {
                serializationAttempted = true;
                if (results != nullptr) {
                    // Later requests need this failure status if serialization throws.
                    resultStatus = v1::ReplyStatus_InternalError;
                    serialized.emplace(results->serialize());
                    resultStatus = serialized->size() > impl->profile.maxResultsBytes
                                       ? v1::ReplyStatus_TooLarge
                                       : v1::ReplyStatus_Ok;
                }
            }
            const auto *resultBytes = serialized ? &*serialized : nullptr;

            // Errors and metadata-only replies must not touch source pixels.
            if (resultStatus != v1::ReplyStatus_Ok || !includeFrame) {
                impl->reply(request, resultStatus, sequence, pts, resultBytes, nullptr);
                continue;
            }

            if (!mappingAttempted) {
                mappingAttempted = true;
                frame = mapFrame();
                if (frame && impl->validFrame(*frame))
                    frameStatus = v1::ReplyStatus_Ok;
            }

            impl->reply(request,
                        frameStatus,
                        sequence,
                        pts,
                        resultBytes,
                        frameStatus == v1::ReplyStatus_Ok ? frame.get() : nullptr);
        } catch (const std::exception &error) {
            // Error delivery is best effort; do not allocate recursively after failure.
            impl->reportError(error.what());
        }
    }
}

} // namespace opk::frame_access
