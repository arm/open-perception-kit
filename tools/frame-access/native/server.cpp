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

#include "frame_access_profile.h"
#include "frame_reply_generated.h"
#include "iox2/iceoryx2.hpp"
#include "open_perception_kit.h"
#include <cassert>
#include <chrono>
#include <iostream>

IOX2_DEFINE_TYPE_NAME(opk::frame_access::v1::FrameReply, "FrameReply");
int main() {
    using namespace iox2;
    namespace wire = opk::frame_access::v1;
    auto config = Config();
    config.global().set_root_path(bb::Path::create(std::getenv("PROBE_RUNTIME")).value());
    config.global().set_prefix(bb::FileName::create("deml2629mix_").value());
    const auto node = NodeBuilder().config(config).create<ServiceType::Ipc>().value();
    const auto service = node.service_builder(ServiceName::create("DEML2629/mixed").value())
                             .request_response<uint64_t, Flatbuffer<wire::FrameReply>>()
                             .response_flatbuffer_schema_path(
                                 bb::FilePath::create(std::getenv("PROBE_SCHEMA_CPP")).value())
                             .max_clients(2)
                             .max_nodes(3)
                             .max_servers(1)
                             .max_active_requests_per_client(2)
                             .max_loaned_requests(2)
                             .max_response_buffer_size(1)
                             .max_borrowed_responses_per_pending_response(1)
                             .enable_safe_overflow_for_requests(false)
                             .enable_safe_overflow_for_responses(false)
                             .enable_fire_and_forget_requests(false)
                             .create()
                             .value();
    const bool grow = std::getenv("PROBE_GROW") != nullptr;
    auto server =
        service.server_builder()
            .initial_reserved_memory(grow ? 1024 : 131072)
            .allocation_strategy(grow ? AllocationStrategy::PowerOfTwo : AllocationStrategy::Static)
            .max_loaned_responses_per_request(1)
            .create()
            .value();
    std::cout << "ready" << std::endl;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    uint64_t index = 0;
    while (index < 10 && std::chrono::steady_clock::now() < deadline) {
        auto request = server.receive().value();
        if (!request.has_value()) {
            node.wait(bb::Duration::from_millis(1)).value();
            continue;
        }
        assert(request->payload() == (index == 7 ? 0U : index == 8 ? 2U : 1U));
        auto response = request->loan_flatbuffer().value();
        auto &builder = response.flatbuffer_builder();
        flatbuffers::Offset<wire::FrameReply> root;
        if (index >= 8) {
            root = wire::CreateFrameReply(builder,
                                          index == 8 ? wire::ReplyStatus_InvalidRequest
                                                     : wire::ReplyStatus_NotReady);
        } else {
            const auto resultsBuffer = open_perception_kit::container::envelope().serialize();
            const auto results = builder.CreateVector(resultsBuffer.data(), resultsBuffer.size());
            flatbuffers::Offset<wire::Frame> frame;
            if (index != 7) {
                const auto format =
                    index < 6 ? static_cast<wire::PixelFormat>(index + 1) : wire::PixelFormat_BGRA8;
                const uint32_t dimension = index == 6 ? 128 : 3;
                const opk::frame_access::DeploymentProfile profile{2, 2, 4, 65536, 1024, 131072};
                const auto bounds =
                    opk::frame_access::constructionBounds(profile, format, dimension, dimension)
                        .value();
                std::array<flatbuffers::Offset<wire::Plane>, 3> planes{};
                for (size_t p = 0; p < bounds.planeCount; ++p) {
                    uint8_t *data = nullptr;
                    const auto pixels =
                        builder.CreateUninitializedVector(bounds.planeBytes[p], &data);
                    for (size_t i = 0; i < bounds.planeBytes[p]; ++i)
                        data[i] = static_cast<uint8_t>((i + p) % 251);
                    if (format == wire::PixelFormat_YUY2)
                        for (size_t row = 0; row < 3; ++row)
                            data[row * 8 + 6] = 0;
                    planes[p] = wire::CreatePlane(builder, pixels);
                }
                const auto planeVector = builder.CreateVector(planes.data(), bounds.planeCount);
                frame = wire::CreateFrame(builder, format, dimension, dimension, planeVector);
            }
            const auto sample = wire::CreateSampleInfo(
                builder,
                index,
                index % 2 ? flatbuffers::nullopt : flatbuffers::Optional<uint64_t>(0));
            root = wire::CreateFrameReply(builder, wire::ReplyStatus_Ok, sample, results, frame);
        }
        send(assume_init(std::move(response), root)).value();
        ++index;
    }
    node.wait(bb::Duration::from_millis(100)).value();
    std::cout << "sent " << index << " replies" << std::endl;
    return index == 10 ? 0 : 1;
}
