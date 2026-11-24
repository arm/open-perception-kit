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

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <thread>

IOX2_DEFINE_TYPE_NAME(opk::frame_access::v1::FrameReply, "FrameReply");

class Image final : public opk::mediaio::VideoFrame {
  public:
    std::array<uint8_t, 16> bytes{};
    std::array<opk::mediaio::DataView, 1> views;
    Image() {
        // Two 3-byte rows with padding that must never be exported.
        bytes = {1, 2, 3, 99, 99, 99, 99, 99, 4, 5, 6, 99, 99, 99, 99, 99};
        views[0] =
            opk::mediaio::DataView::host(bytes.data(), bytes.size(), 8, opk::AccessMode::Read);
    }
    opk::RawImagePixelFormat format() const noexcept override {
        return opk::RawImagePixelFormat::Gray;
    }
    uint32_t width() const noexcept override {
        return 3;
    }
    uint32_t height() const noexcept override {
        return 2;
    }
    opk::mediaio::TimestampNs timestampNs() const noexcept override {
        return 0;
    }
    opk::MemoryType memoryType() const noexcept override {
        return opk::MemoryType::Host;
    }
    std::span<const opk::mediaio::DataView> planes() const noexcept override {
        return views;
    }
    bool canMap(opk::AccessMode) const noexcept override {
        return true;
    }
    std::unique_ptr<opk::mediaio::VideoFrame> map(opk::AccessMode) const override {
        return {};
    }
};

template <typename Pending> auto receive(Pending &pending) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        auto result = pending.receive();
        assert(result.has_value());
        if (result.value().has_value())
            return std::move(result.value().value());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("response deadline expired");
}

int main(int argc, char **argv) {
    assert(argc == 3);
    namespace wire = opk::frame_access::v1;
    constexpr auto ipc = iox2::ServiceType::Ipc;
    std::filesystem::create_directories(argv[1]);
    std::string runtimeTemplate = std::string(argv[1]) + "/run-XXXXXX";
    const auto *runtime = mkdtemp(runtimeTemplate.data());
    assert(runtime != nullptr);
    const opk::frame_access::DeploymentProfile profile{2, 2, 2, 1024, 1024, 4096};
    opk::frame_access::Server server(profile,
                                     "DEML2629/backend",
                                     runtime,
                                     argv[2],
                                     wire::PixelFormat_Gray8,
                                     3,
                                     2,
                                     [](std::string_view) { assert(false); });
    auto config = iox2::Config();
    config.global().set_root_path(iox2::bb::Path::create(runtime).value());
    const auto node = iox2::NodeBuilder().config(config).create<ipc>().value();
    const auto factory =
        node.service_builder(iox2::ServiceName::create("DEML2629/backend").value())
            .request_response<uint64_t, iox2::Flatbuffer<wire::FrameReply>>()
            .response_flatbuffer_schema_path(iox2::bb::FilePath::create(argv[2]).value())
            .open()
            .value();
    auto first = factory.client_builder().create().value();
    auto second = factory.client_builder().create().value();
    open_perception_kit::container::envelope results;
    unsigned maps = 0;
    const auto image = std::make_shared<Image>();
    const auto map = [&]() {
        ++maps;
        return image;
    };

    auto metadata = first.send_copy(0).value();
    server.serve(&results, 0, 0, map);
    const auto metadataReply = receive(metadata);
    assert(metadataReply.payload_root()->status() == wire::ReplyStatus_Ok);
    assert(metadataReply.payload_root()->frame() == nullptr && maps == 0);
    metadata = first.send_copy(1).value();
    auto other = second.send_copy(1).value();
    server.serve(&results, 1, std::nullopt, map);
    const auto a = receive(metadata);
    const auto b = receive(other);
    assert(maps == 1);
    for (const auto *reply : {a.payload_root(), b.payload_root()}) {
        assert(reply->sample()->sequence() == 1 && !reply->sample()->pts_ns().has_value());
        const auto *data = reply->frame()->planes()->Get(0)->data();
        assert(data->size() == 6);
        for (uint8_t i = 0; i < 6; ++i)
            assert(data->Get(i) == i + 1);
    }

    // Existing responses remain owned independently of subsequent requests.
    metadata = first.send_copy(2).value();
    other = second.send_copy(0).value();
    server.serve(nullptr, 2, 0, map);
    assert(receive(metadata).payload_root()->status() == wire::ReplyStatus_InvalidRequest);
    assert(receive(other).payload_root()->status() == wire::ReplyStatus_NotReady);
    assert(maps == 1);

    metadata = first.send_copy(1).value();
    image->views[0] =
        opk::mediaio::DataView::host(image->bytes.data(), 2, 8, opk::AccessMode::Read);
    server.serve(&results, 3, 0, map);
    assert(receive(metadata).payload_root()->status() == wire::ReplyStatus_UnsupportedFrame);
    assert(!server.setLayout(wire::PixelFormat_BGRA8, 100, 100));
    assert(server.setLayout(wire::PixelFormat_Gray8, 3, 2));

    // Three queued attempts must require two completed-buffer callbacks.
    metadata = first.send_copy(0).value();
    auto queued = first.send_copy(0).value();
    other = second.send_copy(0).value();
    server.serve(&results, 4, 0, map);
    auto one = metadata.receive().value();
    auto two = queued.receive().value();
    auto three = other.receive().value();
    assert(unsigned(one.has_value()) + unsigned(two.has_value()) + unsigned(three.has_value()) ==
           2);
    server.serve(&results, 5, 0, map);
    if (!one.has_value())
        assert(receive(metadata).payload_root()->sample()->sequence() == 5);
    if (!two.has_value())
        assert(receive(queued).payload_root()->sample()->sequence() == 5);
    if (!three.has_value())
        assert(receive(other).payload_root()->sample()->sequence() == 5);

    auto tiny = profile;
    tiny.maxResultsBytes = 1;
    opk::frame_access::Server limited(tiny,
                                      "DEML2629/too-large",
                                      runtime,
                                      argv[2],
                                      wire::PixelFormat_Gray8,
                                      3,
                                      2,
                                      [](std::string_view) { assert(false); });
    const auto limitedFactory =
        node.service_builder(iox2::ServiceName::create("DEML2629/too-large").value())
            .request_response<uint64_t, iox2::Flatbuffer<wire::FrameReply>>()
            .response_flatbuffer_schema_path(iox2::bb::FilePath::create(argv[2]).value())
            .open()
            .value();
    auto limitedClient = limitedFactory.client_builder().create().value();
    auto oversized = limitedClient.send_copy(1).value();
    const auto previousMaps = maps;
    limited.serve(&results, 0, 0, map);
    assert(receive(oversized).payload_root()->status() == wire::ReplyStatus_TooLarge);
    assert(maps == previousMaps);

    auto singleAttempt = profile;
    singleAttempt.callbackAttempts = 1;
    opk::frame_access::Server cancelServer(singleAttempt,
                                           "DEML2629/cancel",
                                           runtime,
                                           argv[2],
                                           wire::PixelFormat_Gray8,
                                           3,
                                           2,
                                           [](std::string_view) { assert(false); });
    const auto cancelFactory =
        node.service_builder(iox2::ServiceName::create("DEML2629/cancel").value())
            .request_response<uint64_t, iox2::Flatbuffer<wire::FrameReply>>()
            .response_flatbuffer_schema_path(iox2::bb::FilePath::create(argv[2]).value())
            .open()
            .value();
    auto cancelClient = cancelFactory.client_builder().create().value();
    {
        const auto canceled = cancelClient.send_copy(0).value();
    }
    auto live = cancelClient.send_copy(0).value();
    cancelServer.serve(&results, 6, 0, map);
    // Middleware-discarded cancellations do not consume the returned-request budget.
    assert(receive(live).payload_root()->sample()->sequence() == 6);
}
