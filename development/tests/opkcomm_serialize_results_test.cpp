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

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <nlohmann/json.hpp>
#include <thread>

#include "gst/FrameResultsMeta.h"
#include "opk/Base64.h"
#include "writer.h"

namespace {

using Results = open_perception_kit::FrameResults;
using Meta = opk::FrameResultsMeta;

class RecordingWriter : public Writer {
  public:
    RecordingWriter() : Writer(nullptr, 2) {}
    ~RecordingWriter() override {
        stop();
    }

    std::string waitForRecord() {
        std::unique_lock lock(mutex);
        available.wait_for(lock, std::chrono::seconds(5), [this] { return !record.empty(); });
        return record;
    }

  private:
    bool io_open() override {
        return true;
    }
    void io_close() override {}
    bool publish(const std::string &line) override {
        const std::scoped_lock lock(mutex);
        record = line;
        available.notify_one();
        return true;
    }

    std::mutex mutex;
    std::condition_variable available;
    std::string record;
};

class SerializeResultsTest : public ::testing::Test {
  protected:
    void SetUp() override {
        gst_init(nullptr, nullptr);
        g_autoptr(GError) error = nullptr;
        g_autoptr(GstPlugin) plugin = gst_plugin_load_file(OPKCOMM_PLUGIN_PATH, &error);
        ASSERT_NE(plugin, nullptr) << (error ? error->message : "plugin load failed");
        comm = gst_element_factory_make("opkcomm", nullptr);
        ASSERT_NE(comm, nullptr);
        buffer = gst_buffer_new_allocate(nullptr, 16, nullptr);
        ASSERT_NE(buffer, nullptr);
        gst_buffer_memset(buffer, 0, 0, 16);
        ASSERT_NE(Meta::add(buffer), nullptr);
    }

    void TearDown() override {
        gst_clear_buffer(&buffer);
        gst_clear_object(&comm);
    }

    GstStructure *emit(GstBuffer *const input) {
        GstStructure *response = nullptr;
        g_signal_emit_by_name(comm, "serialize-results", input, &response);
        return response;
    }

    static GBytes *packet(const GstStructure *const response) {
        const auto *const value = gst_structure_get_value(response, "packet");
        return value ? static_cast<GBytes *>(g_value_get_boxed(value)) : nullptr;
    }

    GstElement *comm = nullptr;
    GstBuffer *buffer = nullptr;
};

TEST_F(SerializeResultsTest, ActionReturnsOwnedCachedBytesWithoutStartingWriter) {
    GSignalQuery query{
        .signal_id = 0,
        .signal_name = nullptr,
        .itype = G_TYPE_INVALID,
        .signal_flags = G_SIGNAL_RUN_LAST,
        .return_type = G_TYPE_INVALID,
        .n_params = 0,
        .param_types = nullptr,
    };
    g_signal_query(g_signal_lookup("serialize-results", G_OBJECT_TYPE(comm)), &query);
    ASSERT_NE(query.signal_id, 0U);
    EXPECT_TRUE(query.signal_flags & G_SIGNAL_ACTION);
    ASSERT_EQ(query.n_params, 1U);
    EXPECT_EQ(query.param_types[0], GST_TYPE_BUFFER);
    EXPECT_EQ(query.return_type, GST_TYPE_STRUCTURE);

    auto expected = Meta::readCached(buffer)->bytes();
    const auto *const imageMemory = gst_buffer_peek_memory(buffer, 0);
    g_autoptr(GstStructure) response = emit(buffer);
    ASSERT_NE(response, nullptr);
    EXPECT_STREQ(gst_structure_get_string(response, "status"), "ok");
    ASSERT_EQ(packet(response), expected.get());
    EXPECT_EQ(gst_buffer_peek_memory(buffer, 0), imageMemory);
    EXPECT_FALSE(gst_structure_has_field(response, "message"));
    g_autoptr(GBytes) retained = g_bytes_ref(packet(response));
    expected.reset();
    g_clear_pointer(&response, gst_structure_free);
    gst_clear_buffer(&buffer);
    gst_clear_object(&comm);
    EXPECT_GT(g_bytes_get_size(retained), 0U);
}

TEST_F(SerializeResultsTest, DistinguishesMissingInvalidAndEmptyResults) {
    g_autoptr(GstBuffer) missing = gst_buffer_new();
    g_autoptr(GstStructure) absent = emit(missing);
    ASSERT_NE(absent, nullptr);
    EXPECT_STREQ(gst_structure_get_string(absent, "status"), "no-metadata");
    EXPECT_EQ(packet(absent), nullptr);

    g_autoptr(GstStructure) nullInput = emit(nullptr);
    ASSERT_NE(nullInput, nullptr);
    EXPECT_STREQ(gst_structure_get_string(nullInput, "status"), "error");
    EXPECT_EQ(packet(nullInput), nullptr);

    Meta::mutate<bool>(buffer, [](auto &results) {
        results = Results(std::span<const uint8_t>{});
        return true;
    });
    g_autoptr(GstStructure) invalid = emit(buffer);
    ASSERT_NE(invalid, nullptr);
    EXPECT_STREQ(gst_structure_get_string(invalid, "status"), "error");
    EXPECT_NE(gst_structure_get_string(invalid, "message"), nullptr);
    EXPECT_EQ(packet(invalid), nullptr);

    Meta::mutate<bool>(buffer, [](auto &results) {
        results = Results{};
        return true;
    });
    g_autoptr(GstStructure) empty = emit(buffer);
    ASSERT_NE(empty, nullptr);
    EXPECT_STREQ(gst_structure_get_string(empty, "status"), "ok");
    EXPECT_NE(packet(empty), nullptr);
}

TEST_F(SerializeResultsTest, WriterAndActionSharePacketInEitherOrder) {
    for (const bool actionFirst : {false, true}) {
        SCOPED_TRACE(actionFirst);
        // Begin a fresh metadata generation for each ordering.
        Meta::mutate<bool>(buffer, [](auto &) { return true; });
        RecordingWriter writer;
        ASSERT_TRUE(writer.start());
        const auto snapshot = Meta::readCached(buffer);
        g_autoptr(GstStructure) response = actionFirst ? emit(buffer) : nullptr;
        ASSERT_TRUE(writer.send({.frame_counter = 17, .frameResults = snapshot}));
        const auto record = writer.waitForRecord();
        ASSERT_FALSE(record.empty());
        if (!actionFirst)
            response = emit(buffer);
        ASSERT_NE(response, nullptr);
        ASSERT_NE(packet(response), nullptr);
        const auto bytes = snapshot->bytes();
        EXPECT_EQ(packet(response), bytes.get());
        const auto json = nlohmann::json::parse(record);
        EXPECT_EQ(json["frame_counter"], 17);
        EXPECT_EQ(
            json["frame_results_packet_b64"],
            opk::base64Encode({static_cast<const uint8_t *>(g_bytes_get_data(bytes.get(), nullptr)),
                               g_bytes_get_size(bytes.get())}));
    }
}

TEST_F(SerializeResultsTest, ReadsSuppliedBufferAndRefreshesAfterMutation) {
    g_autoptr(GstStructure) original = emit(buffer);
    ASSERT_NE(original, nullptr);
    Meta::mutate<bool>(buffer, [](auto &results) {
        results.add(open_perception_kit::metadata::BoxDetectionsT{});
        return true;
    });
    g_autoptr(GstStructure) updated = emit(buffer);
    ASSERT_NE(updated, nullptr);
    ASSERT_NE(packet(updated), nullptr);
    EXPECT_NE(packet(original), packet(updated));
    EXPECT_FALSE(g_bytes_equal(packet(original), packet(updated)));
    // Another buffer must never receive the last packet handled by this element.
    g_autoptr(GstBuffer) unrelated = gst_buffer_new();
    g_autoptr(GstStructure) absent = emit(unrelated);
    ASSERT_NE(absent, nullptr);
    EXPECT_STREQ(gst_structure_get_string(absent, "status"), "no-metadata");
}

TEST_F(SerializeResultsTest, WriterDropsInvalidRecordAndContinuesWithTheNextFrame) {
    RecordingWriter writer;
    ASSERT_TRUE(writer.start());
    Meta::mutate<bool>(buffer, [](auto &results) {
        results = Results(std::span<const uint8_t>{});
        return true;
    });
    auto invalid = Meta::readCached(buffer);
    const std::weak_ptr<const opk::FrameResultsPacket> failedSnapshot = invalid;
    ASSERT_TRUE(writer.send({.frame_counter = 1, .frameResults = std::move(invalid)}));
    Meta::mutate<bool>(buffer, [](auto &results) {
        results = Results{};
        return true;
    });
    ASSERT_TRUE(writer.send({.frame_counter = 2, .frameResults = Meta::readCached(buffer)}));
    const auto record = writer.waitForRecord();
    ASSERT_FALSE(record.empty());
    EXPECT_EQ(nlohmann::json::parse(record)["frame_counter"], 2);
    EXPECT_TRUE(failedSnapshot.expired());
}

TEST_F(SerializeResultsTest, IdleWriterDoesNotRetainCompletedSnapshotOrPacket) {
    RecordingWriter writer;
    ASSERT_TRUE(writer.start());
    auto snapshot = Meta::readCached(buffer);
    const std::weak_ptr<const opk::FrameResultsPacket> snapshotOwner = snapshot;
    const std::weak_ptr<GBytes> packetOwner = snapshot->bytes();
    gst_clear_buffer(&buffer);
    ASSERT_TRUE(writer.send({.frame_counter = 1, .frameResults = std::move(snapshot)}));
    ASSERT_FALSE(writer.waitForRecord().empty());

    // publish() wakes the observer just before the worker finishes the iteration.
    // Wait for release, without supplying another frame or stopping the writer.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((!snapshotOwner.expired() || !packetOwner.expired()) &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_TRUE(snapshotOwner.expired());
    EXPECT_TRUE(packetOwner.expired());
}

} // namespace
