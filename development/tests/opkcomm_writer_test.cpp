/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

#include "file_writer.h"
#include "opk/Base64.h"
#include "writer.h"
#include <array>
#include <fcntl.h>
#include <glib/gstdio.h>
#include <gst/gst.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <poll.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <vector>

namespace {
constexpr auto testTimeout = std::chrono::seconds(5);
constexpr auto blockedWriteTimeout = std::chrono::milliseconds(20);
constexpr size_t queueCapacity = 3;

class RecordingWriter final : public Writer {
  public:
    RecordingWriter() : Writer(nullptr, queueCapacity) {}
    ~RecordingWriter() override {
        release();
        stop();
    }
    std::atomic<bool> destinationReady{true};
    bool blockWrite = false;

    bool waitForCount(const size_t count) {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, testTimeout, [&] { return counters.size() == count; });
    }
    bool waitForWrite() {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, testTimeout, [&] { return writing; });
    }
    void release() {
        const std::scoped_lock lock(mutex);
        blockWrite = false;
        changed.notify_all();
    }
    std::vector<uint64_t> records() {
        const std::scoped_lock lock(mutex);
        return counters;
    }

  private:
    bool io_open() override {
        return true;
    }
    void io_close() override {}
    bool ready() override {
        return destinationReady;
    }
    bool publish(const std::string &record) override {
        std::unique_lock lock(mutex);
        writing = true;
        changed.notify_all();
        changed.wait(lock, [&] { return !blockWrite; });
        counters.push_back(nlohmann::json::parse(record).at("frame_counter").get<uint64_t>());
        changed.notify_all();
        return true;
    }
    std::mutex mutex;
    std::condition_variable changed;
    bool writing = false;
    std::vector<uint64_t> counters;
};
} // namespace

TEST(OpkCommWriter, TransfersBoundedUnstartedJobsInOrder) {
    RecordingWriter current;
    RecordingWriter replacement;
    current.destinationReady = false;
    ASSERT_TRUE(current.start());
    ASSERT_TRUE(replacement.start(true));
    for (uint64_t counter = 0; counter < queueCapacity; ++counter)
        ASSERT_TRUE(current.send({counter, {}}));
    EXPECT_FALSE(current.send({queueCapacity, {}}));
    ASSERT_TRUE(current.pauseUntil(std::chrono::steady_clock::now() + testTimeout));
    current.transferTo(replacement);
    replacement.resume();
    ASSERT_TRUE(replacement.waitForCount(queueCapacity));
    EXPECT_EQ(replacement.records(), (std::vector<uint64_t>{0, 1, 2}));
    EXPECT_TRUE(current.records().empty());
}

TEST(OpkCommWriter, TimeoutResumesOldQueueAndCannotSwitchLater) {
    RecordingWriter current;
    RecordingWriter replacement;
    current.blockWrite = true;
    ASSERT_TRUE(current.start());
    ASSERT_TRUE(replacement.start(true));
    ASSERT_TRUE(current.send({0, {}}));
    ASSERT_TRUE(current.waitForWrite());
    ASSERT_TRUE(current.send({1, {}}));
    EXPECT_FALSE(current.pauseUntil(std::chrono::steady_clock::now() + blockedWriteTimeout));
    EXPECT_TRUE(current.records().empty());
    current.release();
    ASSERT_TRUE(current.waitForCount(2));
    EXPECT_EQ(current.records(), (std::vector<uint64_t>{0, 1}));
    EXPECT_TRUE(replacement.records().empty());
    ASSERT_TRUE(current.pauseUntil(std::chrono::steady_clock::now() + testTimeout));
}

TEST(OpkCommWriter, PauseWaitsForAnInProgressReadinessCheck) {
    JobQueue queue(queueCapacity);
    ASSERT_TRUE(queue.try_push({0, {}}));
    std::promise<void> readinessEntered;
    std::promise<void> releaseReadiness;
    auto entered = readinessEntered.get_future();
    auto release = releaseReadiness.get_future();
    auto popped = std::async(std::launch::async, [&] {
        OpkCommJob job;
        return queue.pop(job, [&] {
            readinessEntered.set_value();
            release.wait();
            return true;
        });
    });
    EXPECT_EQ(entered.wait_for(testTimeout), std::future_status::ready);
    const bool paused = queue.pauseUntil(std::chrono::steady_clock::now() + blockedWriteTimeout);
    if (paused)
        queue.resume();
    releaseReadiness.set_value();
    EXPECT_FALSE(paused);
    EXPECT_TRUE(popped.get());
}

class OpkCommFileWriter : public ::testing::Test {
  protected:
    void SetUp() override {
        gst_init(nullptr, nullptr);
        directory = g_dir_make_tmp("opk-writer-XXXXXX", nullptr);
        ASSERT_NE(directory, nullptr);
        path = std::string(directory) + "/output";
        ASSERT_EQ(mkfifo(path.c_str(), S_IRUSR | S_IWUSR), 0);
        reader = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        ASSERT_GE(reader, 0);
    }
    void TearDown() override {
        if (reader >= 0)
            close(reader);
        g_remove(path.c_str());
        if (directory)
            g_rmdir(directory);
        g_free(directory);
    }
    gchar *directory = nullptr;
    std::string path;
    int reader = -1;
};

TEST_F(OpkCommFileWriter, PartialRecordRemainsCompleteAfterHandoffTimeout) {
    constexpr size_t payloadBytes = 1024 * 1024;
    constexpr size_t readBufferBytes = 16 * 1024;
    constexpr int pollTimeoutMs = 100;
    auto results = std::make_shared<open_perception_kit::FrameResults>();
    open_perception_kit::appendPerformanceOverlay(*results, {std::string(payloadBytes, 'x')});
    const auto expectedPacket = opk::base64Encode(open_perception_kit::serialize(*results));
    FileWriter writer(nullptr, path, queueCapacity);
    ASSERT_TRUE(writer.start());
    ASSERT_TRUE(writer.send({0, results}));
    pollfd input{reader, POLLIN, 0};
    ASSERT_GT(
        poll(&input, 1, std::chrono::duration_cast<std::chrono::milliseconds>(testTimeout).count()),
        0);
    EXPECT_FALSE(writer.pauseUntil(std::chrono::steady_clock::now() + blockedWriteTimeout));
    std::string received;
    std::array<char, readBufferBytes> bytes{};
    const auto deadline = std::chrono::steady_clock::now() + testTimeout;
    while (std::chrono::steady_clock::now() < deadline && !received.ends_with('\n')) {
        if (poll(&input, 1, pollTimeoutMs) > 0) {
            const auto count = read(reader, bytes.data(), bytes.size());
            if (count > 0)
                received.append(bytes.data(), static_cast<size_t>(count));
        }
    }
    writer.stop();
    ASSERT_TRUE(received.ends_with('\n'));
    const auto record = nlohmann::json::parse(received);
    EXPECT_EQ(record.at("frame_counter"), 0);
    EXPECT_EQ(record.at("frame_results_packet_b64"), expectedPacket);
}
