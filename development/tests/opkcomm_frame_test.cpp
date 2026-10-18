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

#include "../elements/opkcomm/frame_export.h"
#include <arpa/inet.h>
#include <glib/gstdio.h>
#include <gst/FrameResultsMeta.h>
#include <gst/GstMetaWrapper.h>
#include <gst/base/gstbasetransform.h>
#include <gst/check/gstharness.h>
#include <gst/video/video.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <opk/Base64.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
GstVideoInfo videoInfo(GstVideoFormat format = GST_VIDEO_FORMAT_BGRA) {
    gst_init(nullptr, nullptr);
    GstVideoInfo info{};
    gst_video_info_set_format(&info, format, 2, 2);
    return info;
}
} // namespace

TEST(OpkCommFrame, PacksPaddedRowsAndOwnsPixels) {
    auto info = videoInfo();
    auto *buffer = gst_buffer_new_allocate(nullptr, 24, nullptr);
    const uint8_t source[] = {1, 2,  3,  4,  5,  6,  7,  8,  99, 99, 99, 99,
                              9, 10, 11, 12, 13, 14, 15, 16, 99, 99, 99, 99};
    gst_buffer_fill(buffer, 0, source, sizeof(source));
    gsize offsets[GST_VIDEO_MAX_PLANES]{};
    gint strides[GST_VIDEO_MAX_PLANES] = {12};
    ASSERT_NE(
        gst_buffer_add_video_meta_full(
            buffer, GST_VIDEO_FRAME_FLAG_NONE, GST_VIDEO_FORMAT_BGRA, 2, 2, 1, offsets, strides),
        nullptr);
    auto frame = captureFrame(buffer, info);
    gst_buffer_memset(buffer, 0, 0, 24);
    gst_buffer_unref(buffer);
    EXPECT_EQ(frame.width, 2U);
    EXPECT_EQ(frame.height, 2U);
    EXPECT_EQ(frame.pixels,
              (std::vector<uint8_t>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}));
}

TEST(OpkCommFrame, RejectsUnsupportedFormatAndMissingMemory) {
    auto info = videoInfo(GST_VIDEO_FORMAT_RGB);
    auto *buffer = gst_buffer_new();
    EXPECT_THROW(captureFrame(buffer, info), std::runtime_error);
    info = videoInfo();
    EXPECT_THROW(captureFrame(buffer, info), std::runtime_error);
    gst_buffer_unref(buffer);
}

TEST(OpkCommFrame, ImageIsSeparateFromUnchangedResultsPacket) {
    auto results = std::make_shared<open_perception_kit::FrameResults>();
    OpkCommJob job{.frame_counter = 42, .frameResults = results, .frame = std::nullopt};
    const auto original = nlohmann::json::parse(serializeJob(job));
    EXPECT_EQ(original.size(), 3U);
    EXPECT_FALSE(original.contains("frame"));
    job.frame = OpkCommFrame{1, 1, {1, 2, 3, 4}};
    const auto record = nlohmann::json::parse(serializeJob(job));
    EXPECT_EQ(record["frame_results_packet_b64"], original["frame_results_packet_b64"]);
    EXPECT_EQ(record["frame_counter"], 42);
    EXPECT_EQ(record["frame"]["data_b64"], "AQIDBA==");
    EXPECT_EQ(record["frame"]["encoding"], "raw+base64");
    EXPECT_EQ(record["frame"]["pixel_format"], "BGRA");
    EXPECT_EQ(record["frame"]["stride_bytes"], 4);
    EXPECT_EQ(record["frame"].size(), 6U);
}

TEST(OpkCommFrame, QueuedResultsSurviveMetadataMutation) {
    videoInfo();
    auto *buffer = gst_buffer_new();
    auto results = std::make_shared<open_perception_kit::FrameResults>();
    ASSERT_NE(opk::FrameResultsMeta::add(buffer, std::move(results)), nullptr);
    OpkCommJob job{.frameResults = opk::FrameResultsMeta::read(buffer), .frame = std::nullopt};
    const auto before = serializeJob(job);
    const auto mutated = opk::FrameResultsMeta::mutate<bool>(buffer, [](auto &env) {
        open_perception_kit::metadata::FrameContextT context;
        env.add(std::move(context));
        return true;
    });
    EXPECT_TRUE(std::get<bool>(mutated));
    EXPECT_EQ(serializeJob(job), before);
    EXPECT_NE(
        opk::base64Encode(open_perception_kit::serialize(*opk::FrameResultsMeta::read(buffer))),
        nlohmann::json::parse(before)["frame_results_packet_b64"].get<std::string>());
    gst_buffer_unref(buffer);
}

TEST(OpkCommFrame, QueueDropsWholeJobAndReleasesOnStop) {
    JobQueue queue(1);
    auto results = std::make_shared<open_perception_kit::FrameResults>();
    std::weak_ptr<const open_perception_kit::FrameResults> weak = results;
    EXPECT_TRUE(queue.try_push({.frame_counter = 1,
                                .frameResults = std::move(results),
                                .frame = OpkCommFrame{1, 1, {1, 2, 3, 4}}}));
    EXPECT_FALSE(
        queue.try_push({.frame_counter = 2, .frameResults = nullptr, .frame = std::nullopt}));
    EXPECT_FALSE(weak.expired());
    queue.stop();
    EXPECT_TRUE(weak.expired());
    OpkCommJob job;
    EXPECT_FALSE(queue.pop(job));
    queue.reset();
    EXPECT_TRUE(
        queue.try_push({.frame_counter = 3, .frameResults = nullptr, .frame = std::nullopt}));
    EXPECT_TRUE(queue.pop(job));
    EXPECT_EQ(job.frame_counter, 3U);
}

namespace {
class Receiver {
    int fd = -1;

  public:
    explicit Receiver(unsigned port, bool websocket = true) {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0)
            throw std::runtime_error("socket failed");
        timeval timeout{3, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
            close(fd);
            throw std::runtime_error("Publisher connection failed");
        }
        if (!websocket) {
            return;
        }
        const std::string request =
            "GET /ws HTTP/1.1\r\nHost: localhost\r\n"
            "Upgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
        if (send(fd, request.data(), request.size(), MSG_NOSIGNAL) != ssize_t(request.size())) {
            close(fd);
            throw std::runtime_error("handshake write failed");
        }
        std::string reply;
        try {
            while (!reply.ends_with("\r\n\r\n") && reply.size() < 4096)
                reply += read(1);
            if (!reply.starts_with("HTTP/1.1 101"))
                throw std::runtime_error("handshake rejected");
        } catch (...) {
            close(fd);
            throw;
        }
    }
    ~Receiver() {
        if (fd >= 0)
            close(fd);
    }
    std::string read(size_t count) {
        std::string bytes(count, '\0');
        size_t offset = 0;
        while (offset < count) {
            auto n = recv(fd, bytes.data() + offset, count - offset, 0);
            if (n <= 0)
                throw std::runtime_error("Publisher socket read failed or timed out");
            offset += size_t(n);
        }
        return bytes;
    }
    nlohmann::json record() {
        auto header = read(2);
        if (uint8_t(header[0]) != 0x81)
            throw std::runtime_error("expected text message");
        size_t size = uint8_t(header[1]);
        if (size >= 128)
            throw std::runtime_error("server frame is masked");
        if (size == 126 || size == 127) {
            auto length = read(size == 126 ? 2 : 8);
            size = 0;
            for (unsigned char byte : length)
                size = (size << 8) | byte;
        }
        return nlohmann::json::parse(read(size));
    }
};

GstElement *makePublisher() {
    videoInfo();
    GError *error = nullptr;
    auto *plugin = gst_plugin_load_file(OPK_COMM_PLUGIN, &error);
    if (!plugin) {
        std::string message = error ? error->message : "plugin load failed";
        g_clear_error(&error);
        throw std::runtime_error(message);
    }
    gst_object_unref(plugin);
    return gst_element_factory_make("opkcomm", nullptr);
}

unsigned freePort() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (fd < 0 || bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address))) {
        if (fd >= 0)
            close(fd);
        throw std::runtime_error("test port allocation failed");
    }
    socklen_t length = sizeof(address);
    getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length);
    close(fd);
    return ntohs(address.sin_port);
}
} // namespace

TEST(OpkCommFrame, PassthroughPublishesPairedPixelsAndSupportsRestart) {
    videoInfo();
    auto *element = makePublisher();
    ASSERT_NE(element, nullptr);
    gst_object_ref_sink(element);
    for (bool enabled : {false, true}) {
        auto port = freePort();
        g_object_set(element, "method", 2, "ws-port", port, "include-frame", enabled, nullptr);
        auto *harness = gst_harness_new_with_element(element, "sink", "src");
        gst_harness_set_src_caps_str(harness,
                                     "video/x-raw,format=BGRA,width=2,height=2,framerate=1/1");
        gst_harness_play(harness);
        {
            Receiver receiver(port);
            auto *buffer = gst_buffer_new_allocate(nullptr, 16, nullptr);
            gst_buffer_memset(buffer, 0, 7, 16);
            auto results = std::make_shared<open_perception_kit::FrameResults>();
            const auto packet = opk::base64Encode(open_perception_kit::serialize(*results));
            ASSERT_NE(opk::FrameResultsMeta::add(buffer, std::move(results)), nullptr);
            ASSERT_EQ(gst_harness_push(harness, buffer), GST_FLOW_OK);
            auto *output = gst_harness_pull(harness);
            ASSERT_NE(output, nullptr);
            EXPECT_EQ(gst_buffer_get_size(output), 16U);
            gst_buffer_unref(output);
            const auto record = receiver.record();
            EXPECT_EQ(record["frame_results_packet_b64"].get<std::string>(), packet);
            EXPECT_EQ(record.contains("frame"), enabled);
            if (enabled) {
                EXPECT_EQ(record["frame"]["data_b64"].get<std::string>(),
                          opk::base64Encode(std::vector<uint8_t>(16, 7)));
                EXPECT_EQ(record["frame"]["width"], 2);
                // Startup-only property changes are ignored while running.
                g_object_set(element, "include-frame", FALSE, nullptr);
                gboolean actual = FALSE;
                g_object_get(element, "include-frame", &actual, nullptr);
                EXPECT_TRUE(actual);
            }
        }
        gst_harness_teardown(harness);
    }
    gst_object_unref(element);
}

TEST(OpkCommFrame, CapturesAndSerializesLargeFrames) {
    auto info = videoInfo();
    ASSERT_TRUE(gst_video_info_set_format(&info, GST_VIDEO_FORMAT_BGRA, 1600, 1024));
    auto *buffer = gst_buffer_new_allocate(nullptr, info.size, nullptr);
    ASSERT_NE(buffer, nullptr);
    gst_buffer_memset(buffer, 0, 7, info.size);
    auto frame = captureFrame(buffer, info);
    gst_buffer_unref(buffer);
    EXPECT_GT(frame.pixels.size(), 6U * 1024U * 1024U);
    OpkCommJob job{.frameResults = nullptr, .frame = std::move(frame)};
    const auto record = serializeJob(job);
    EXPECT_GT(record.size(), 8U * 1024U * 1024U);
    EXPECT_EQ(nlohmann::json::parse(record)["frame"]["width"], 1600);
}

TEST(OpkCommFrame, PublishesPairedFramesThroughFileAndTcp) {
    videoInfo();
    gchar *raw_name = nullptr;
    const int fd = g_file_open_tmp("opkcomm-frame-XXXXXX", &raw_name, nullptr);
    ASSERT_GE(fd, 0);
    close(fd);
    std::unique_ptr<gchar, decltype(&g_free)> name(raw_name, &g_free);
    for (int method : {1, 3}) {
        auto *element = makePublisher();
        ASSERT_NE(element, nullptr);
        const auto port = freePort();
        g_object_set(element,
                     "method",
                     method,
                     "file-name",
                     name.get(),
                     "tcp-port",
                     port,
                     "include-frame",
                     TRUE,
                     nullptr);
        auto *harness = gst_harness_new_with_element(element, "sink", "src");
        gst_harness_set_src_caps_str(harness,
                                     "video/x-raw,format=BGRA,width=2,height=2,framerate=1/1");
        gst_harness_play(harness);
        std::optional<Receiver> receiver;
        if (method == 3)
            receiver.emplace(port, false);
        auto *buffer = gst_buffer_new_allocate(nullptr, 16, nullptr);
        gst_buffer_memset(buffer, 0, 7, 16);
        auto results = std::make_shared<open_perception_kit::FrameResults>();
        const auto packet = opk::base64Encode(open_perception_kit::serialize(*results));
        ASSERT_NE(opk::FrameResultsMeta::add(buffer, std::move(results)), nullptr);
        ASSERT_EQ(gst_harness_push(harness, buffer), GST_FLOW_OK);
        gst_buffer_unref(gst_harness_pull(harness));
        std::string line;
        if (receiver) {
            while (!line.ends_with("\n"))
                line += receiver->read(1);
        } else {
            for (int attempt = 0; attempt < 300 && !line.ends_with("\n"); ++attempt) {
                gchar *contents = nullptr;
                gsize size = 0;
                if (g_file_get_contents(name.get(), &contents, &size, nullptr)) {
                    line.assign(contents, size);
                    g_free(contents);
                }
                if (!line.ends_with("\n"))
                    g_usleep(10000);
            }
        }
        ASSERT_TRUE(line.ends_with("\n"));
        const auto record = nlohmann::json::parse(line);
        EXPECT_EQ(record["frame_results_packet_b64"].get<std::string>(), packet);
        EXPECT_EQ(record["frame"]["data_b64"].get<std::string>(),
                  opk::base64Encode(std::vector<uint8_t>(16, 7)));
        gst_harness_teardown(harness);
    }
    g_unlink(name.get());
}

TEST(OpkCommFrame, RejectsBadCapsAndMissingResults) {
    auto *element = makePublisher();
    ASSERT_NE(element, nullptr);
    g_object_set(element, "include-frame", TRUE, "method", 2, "ws-port", freePort(), nullptr);
    auto *trans = GST_BASE_TRANSFORM(element);
    auto *klass = GST_BASE_TRANSFORM_GET_CLASS(trans);
    auto *caps = gst_caps_from_string("video/x-raw,format=RGB,width=2,height=2,framerate=1/1");
    EXPECT_FALSE(klass->set_caps(trans, caps, caps));
    gst_caps_unref(caps);
    caps = gst_caps_from_string("video/x-raw,format=BGRA,width=2,height=2,framerate=1/1");
    EXPECT_TRUE(klass->set_caps(trans, caps, caps));
    gst_caps_unref(caps);
    ASSERT_TRUE(klass->start(trans));
    auto *buffer = gst_buffer_new_allocate(nullptr, 16, nullptr);
    EXPECT_EQ(klass->transform_ip(trans, buffer), GST_FLOW_ERROR);
    gst_buffer_unref(buffer);
    EXPECT_TRUE(klass->stop(trans));
    gst_object_unref(element);
}
