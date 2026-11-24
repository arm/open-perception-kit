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

#include "frame_reply_generated.h"
#include "gst/FrameResultsMeta.h"
#include "iox2/iceoryx2.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <filesystem>
#include <gst/app/gstappsrc.h>
#include <thread>

IOX2_DEFINE_TYPE_NAME(opk::frame_access::v1::FrameReply, "FrameReply");
int main(int argc, char **argv) {
    assert(argc == 4);
    gst_init(nullptr, nullptr);
    GError *error = nullptr;
    auto *plugin = gst_plugin_load_file(argv[1], &error);
    if (!plugin)
        throw std::runtime_error(error ? error->message : "plugin unavailable");
    std::filesystem::create_directories(argv[2]);
    std::string runtimeTemplate = std::string(argv[2]) + "/run-XXXXXX";
    const auto *runtime = mkdtemp(runtimeTemplate.data());
    assert(runtime != nullptr);
    auto *pipeline = gst_pipeline_new(nullptr);
    auto *source = gst_element_factory_make("appsrc", "input");
    auto *comm = gst_element_factory_make("opkcomm", "export");
    auto *sink = gst_element_factory_make("fakesink", "output");
    assert(source && comm && sink);
    const char *profile =
        R"({"clients":2,"active_requests_per_client":2,"callback_attempts":2,"max_image_bytes":1024,"max_results_bytes":1024,"max_construction_bytes":4096})";
    g_object_set(comm,
                 "method",
                 4,
                 "frame-endpoint",
                 "DEML2629/pipeline",
                 "runtime-path",
                 runtime,
                 "frame-profile",
                 profile,
                 nullptr);
    g_object_set(source, "is-live", TRUE, "format", GST_FORMAT_TIME, nullptr);
    auto *caps = gst_caps_from_string(
        "video/x-raw,format=GRAY8,width=3,height=2,framerate=1/1,interlace-mode=progressive");
    gst_app_src_set_caps(GST_APP_SRC(source), caps);
    gst_caps_unref(caps);
    g_object_set(sink, "async", FALSE, "sync", FALSE, "signal-handoffs", TRUE, nullptr);
    std::atomic<unsigned> completed{0};
    g_signal_connect(sink,
                     "handoff",
                     G_CALLBACK(+[](GstElement *, GstBuffer *, GstPad *, gpointer data) {
                         ++*static_cast<std::atomic<unsigned> *>(data);
                     }),
                     &completed);
    gst_bin_add_many(GST_BIN(pipeline), source, comm, sink, nullptr);
    assert(gst_element_link_many(source, comm, sink, nullptr));
    assert(gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
    const auto push = [&] {
        auto *buffer = gst_buffer_new_allocate(nullptr, 8, nullptr);
        const uint8_t pixels[] = {1, 2, 3, 99, 4, 5, 6, 99};
        gst_buffer_fill(buffer, 0, pixels, sizeof(pixels));
        GST_BUFFER_PTS(buffer) = 0;
        opk::FrameResultsMeta::add(buffer, std::make_shared<open_perception_kit::FrameResults>());
        assert(gst_app_src_push_buffer(GST_APP_SRC(source), buffer) == GST_FLOW_OK);
    };
    push();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (completed == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(completed == 1);
    gboolean ready = FALSE;
    g_object_get(comm, "frame-access-ready", &ready, nullptr);
    assert(ready);
    auto config = iox2::Config();
    config.global().set_root_path(iox2::bb::Path::create(runtime).value());
    const auto node = iox2::NodeBuilder().config(config).create<iox2::ServiceType::Ipc>().value();
    const auto factory =
        node.service_builder(iox2::ServiceName::create("DEML2629/pipeline").value())
            .request_response<uint64_t, iox2::Flatbuffer<opk::frame_access::v1::FrameReply>>()
            .response_flatbuffer_schema_path(iox2::bb::FilePath::create(argv[3]).value())
            .open()
            .value();
    auto client = factory.client_builder().create().value();
    auto pending = client.send_copy(1).value();
    push();
    const auto receiveDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < receiveDeadline) {
        auto response = pending.receive().value();
        if (response.has_value()) {
            const auto *reply = response->payload_root();
            assert(reply->status() == opk::frame_access::v1::ReplyStatus_Ok);
            assert(reply->sample()->sequence() == 1 && reply->sample()->pts_ns().value() == 0);
            const auto *plane = reply->frame()->planes()->Get(0)->data();
            assert(plane->size() == 6);
            for (unsigned i = 0; i < 6; ++i)
                assert(plane->Get(i) == i + 1);
            // Unsupported pixel layouts must not prevent metadata-only replies.
            auto *interlaced = gst_caps_from_string(
                "video/x-raw,format=GRAY8,width=3,height=2,framerate=1/1,interlace-mode=interleaved");
            gst_app_src_set_caps(GST_APP_SRC(source), interlaced);
            gst_caps_unref(interlaced);
            pending = client.send_copy(0).value();
            auto withPixels = client.send_copy(1).value();
            push();
            bool metadataReceived = false;
            bool rejectionReceived = false;
            const auto pairDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while ((!metadataReceived || !rejectionReceived) && std::chrono::steady_clock::now() < pairDeadline) {
                if (!metadataReceived) {
                    auto metadata = pending.receive().value();
                    if (metadata.has_value()) {
                        assert(metadata->payload_root()->status() == opk::frame_access::v1::ReplyStatus_Ok);
                        assert(metadata->payload_root()->frame() == nullptr);
                        metadataReceived = true;
                    }
                }
                if (!rejectionReceived) {
                    auto rejected = withPixels.receive().value();
                    if (rejected.has_value()) {
                        assert(rejected->payload_root()->status() == opk::frame_access::v1::ReplyStatus_UnsupportedFrame);
                        assert(rejected->payload_root()->sample() == nullptr);
                        rejectionReceived = true;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            assert(metadataReceived && rejectionReceived);
            gst_element_set_state(pipeline, GST_STATE_NULL);
            g_object_get(comm, "frame-access-ready", &ready, nullptr);
            assert(!ready);
            gst_object_unref(pipeline);
            gst_object_unref(plugin);
            for (unsigned i = 0; i < 6; ++i)
                assert(plane->Get(i) == i + 1);
            std::puts("PASS actual GStreamer caps, padded mapping, sequence/PTS and retained "
                      "response after stop");
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("pipeline response deadline");
}
