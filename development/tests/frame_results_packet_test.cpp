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

#include <array>
#include <barrier>
#include <future>
#include <stdexcept>
#include <utility>

#include "gst/FrameResultsMeta.h"

namespace {

using Results = open_perception_kit::FrameResults;
using Meta = opk::FrameResultsMeta;
using Detection = open_perception_kit::metadata::BoxDetectionsT;

Results decode(GBytes *const packet) {
    return Results(std::span<const uint8_t>{
        static_cast<const uint8_t *>(g_bytes_get_data(packet, nullptr)), g_bytes_get_size(packet)});
}

class FrameResultsPacketTest : public ::testing::Test {
  protected:
    void SetUp() override {
        gst_init(nullptr, nullptr);
        buffer = gst_buffer_new_allocate(nullptr, 16, nullptr);
        ASSERT_NE(buffer, nullptr);
        Results results;
        results.add(Detection{});
        ASSERT_NE(Meta::add(buffer, std::move(results)), nullptr);
    }

    void TearDown() override {
        gst_clear_buffer(&buffer);
    }

    void append(GstBuffer *const target) {
        const auto result = Meta::mutate<bool>(target, [](auto &results) {
            results.add(Detection{});
            return true;
        });
        ASSERT_TRUE(std::holds_alternative<bool>(result));
        EXPECT_TRUE(std::get<bool>(result));
    }

    GstBuffer *buffer = nullptr;
};

TEST_F(FrameResultsPacketTest, ReusesSameAllocationAcrossReadsAndBufferCopies) {
    // Copies made before the first request must share the initially empty cache.
    g_autoptr(GstBuffer) earlyCopy = gst_buffer_copy(buffer);
    ASSERT_NE(earlyCopy, nullptr);
    const auto snapshot = Meta::readCached(buffer);
    const auto packet = snapshot->bytes();
    EXPECT_EQ(Meta::readCached(earlyCopy), snapshot);
    EXPECT_EQ(Meta::readCached(earlyCopy)->bytes().get(), packet.get());

    g_autoptr(GstBuffer) lateCopy = gst_buffer_copy_deep(buffer);
    ASSERT_NE(lateCopy, nullptr);
    EXPECT_EQ(Meta::readCached(lateCopy), snapshot);
    EXPECT_EQ(Meta::readCached(buffer)->bytes().get(), packet.get());
    EXPECT_EQ(decode(packet.get()).count<Detection>(), 1U);
}

TEST_F(FrameResultsPacketTest, ConcurrentFirstRequestsShareSnapshotAndPacket) {
    constexpr std::size_t readers = 8;
    std::barrier<> start(static_cast<std::ptrdiff_t>(readers));
    g_autoptr(GstBuffer) copy = gst_buffer_copy(buffer);
    ASSERT_NE(copy, nullptr);
    std::array<std::future<std::shared_ptr<GBytes>>, readers> requests;
    for (std::size_t i = 0; i < readers; ++i) {
        auto *const target = i % 2 == 0 ? buffer : copy;
        requests[i] = std::async(std::launch::async, [&start, target] {
            start.arrive_and_wait();
            return Meta::readCached(target)->bytes();
        });
    }
    const auto packet = requests.front().get();
    for (std::size_t i = 1; i < readers; ++i)
        EXPECT_EQ(requests[i].get().get(), packet.get());
    EXPECT_EQ(decode(packet.get()).count<Detection>(), 1U);
}

TEST_F(FrameResultsPacketTest, QueuedSnapshotSurvivesMutationBeforeSerialization) {
    const auto queued = Meta::readCached(buffer);
    append(buffer);
    const auto current = Meta::readCached(buffer);
    EXPECT_NE(current, queued);
    gst_clear_buffer(&buffer);
    // A writer may not request bytes until after downstream processing finishes.
    EXPECT_EQ(decode(queued->bytes().get()).count<Detection>(), 1U);
    EXPECT_EQ(decode(current->bytes().get()).count<Detection>(), 2U);
}

TEST_F(FrameResultsPacketTest, MutationDetachesOnlyTheChangedBranch) {
    g_autoptr(GstBuffer) branch = gst_buffer_copy(buffer);
    ASSERT_NE(branch, nullptr);
    const auto original = Meta::readCached(buffer)->bytes();
    append(branch);
    const auto changed = Meta::readCached(branch)->bytes();
    EXPECT_NE(changed.get(), original.get());
    EXPECT_EQ(Meta::readCached(buffer)->bytes().get(), original.get());
    EXPECT_EQ(decode(original.get()).count<Detection>(), 1U);
    EXPECT_EQ(decode(changed.get()).count<Detection>(), 2U);
}

TEST_F(FrameResultsPacketTest, BranchesMutatedBeforeFirstReadHaveIndependentCaches) {
    g_autoptr(GstBuffer) branch = gst_buffer_copy(buffer);
    ASSERT_NE(branch, nullptr);
    append(branch);
    EXPECT_EQ(decode(Meta::readCached(branch)->bytes().get()).count<Detection>(), 2U);
    EXPECT_EQ(decode(Meta::readCached(buffer)->bytes().get()).count<Detection>(), 1U);
}

TEST_F(FrameResultsPacketTest, MutationInvalidatesEvenWithoutRetainedReader) {
    std::weak_ptr<const opk::FrameResultsPacket> oldSnapshot = Meta::readCached(buffer);
    std::weak_ptr<GBytes> oldPacket = Meta::readCached(buffer)->bytes();
    ASSERT_FALSE(oldSnapshot.expired());
    ASSERT_FALSE(oldPacket.expired());
    append(buffer);
    EXPECT_TRUE(oldSnapshot.expired());
    EXPECT_TRUE(oldPacket.expired());
    EXPECT_EQ(decode(Meta::readCached(buffer)->bytes().get()).count<Detection>(), 2U);
}

TEST_F(FrameResultsPacketTest, NoOpAndThrowingMutationsCannotLeaveStaleCache) {
    const auto before = Meta::readCached(buffer)->bytes();
    Meta::mutate<bool>(buffer, [](auto &) { return true; });
    const auto afterNoOp = Meta::readCached(buffer)->bytes();
    EXPECT_NE(before.get(), afterNoOp.get());
    EXPECT_TRUE(g_bytes_equal(before.get(), afterNoOp.get()));

    EXPECT_THROW(Meta::mutate<bool>(buffer,
                                    [](auto &results) -> bool {
                                        results.add(Detection{});
                                        throw std::runtime_error("mutation failed after appending");
                                    }),
                 std::runtime_error);
    const auto afterFailure = Meta::readCached(buffer)->bytes();
    EXPECT_NE(afterNoOp.get(), afterFailure.get());
    EXPECT_EQ(decode(afterNoOp.get()).count<Detection>(), 1U);
    EXPECT_EQ(decode(afterFailure.get()).count<Detection>(), 2U);
}

TEST_F(FrameResultsPacketTest, PacketOutlivesBufferWithoutRetainingImageOrResults) {
    bool bufferFreed = false;
    g_autoptr(GstBuffer) owned = std::exchange(buffer, nullptr);
    gst_mini_object_weak_ref(
        GST_MINI_OBJECT(owned),
        [](gpointer data, GstMiniObject *) { *static_cast<bool *>(data) = true; },
        &bufferFreed);
    const std::weak_ptr<const Results> results = Meta::read(owned);
    auto packet = Meta::readCached(owned)->bytes();
    const std::weak_ptr<GBytes> packetOwner = packet;
    g_clear_pointer(&owned, gst_buffer_unref);
    EXPECT_TRUE(bufferFreed);
    EXPECT_TRUE(results.expired());
    EXPECT_EQ(decode(packet.get()).count<Detection>(), 1U);
    packet.reset();
    EXPECT_TRUE(packetOwner.expired());
}

TEST_F(FrameResultsPacketTest, SuccessfulSerializationReleasesSourceWhileCacheRemainsAlive) {
    const std::weak_ptr<const Results> source = Meta::read(buffer);
    const auto snapshot = Meta::readCached(buffer);
    gst_clear_buffer(&buffer);
    ASSERT_FALSE(source.expired());

    const auto packet = snapshot->bytes();
    EXPECT_TRUE(source.expired());
    EXPECT_EQ(snapshot->bytes(), packet);
    EXPECT_EQ(decode(packet.get()).count<Detection>(), 1U);
}

TEST_F(FrameResultsPacketTest, ViewSharesPacketMemoryAndSurvivesMetadataInvalidation) {
    const auto snapshot = Meta::readCached(buffer);
    const auto view = snapshot->view(); // First access also initializes the packet lazily.
    const auto packet = snapshot->bytes();
    EXPECT_EQ(view.data(), g_bytes_get_data(packet.get(), nullptr));
    EXPECT_EQ(view.size(), g_bytes_get_size(packet.get()));

    append(buffer);
    EXPECT_EQ(Results(Meta::readCached(buffer)->view()).count<Detection>(), 2U);
    gst_clear_buffer(&buffer);
    // The retained snapshot owns the old span independently of the buffer/cache slot.
    const Results decoded(view);
    EXPECT_TRUE(decoded.valid());
    EXPECT_EQ(decoded.count<Detection>(), 1U);
    EXPECT_EQ(snapshot->view().data(), view.data());
}

TEST_F(FrameResultsPacketTest, AcquiringSnapshotIsLazyAndFailuresDoNotReturnPackets) {
    Meta::mutate<bool>(buffer, [](auto &results) {
        results = Results(std::span<const uint8_t>{}); // deliberately invalid envelope
        return true;
    });
    const std::weak_ptr<const Results> source = Meta::read(buffer);
    std::shared_ptr<const opk::FrameResultsPacket> invalid;
    ASSERT_NO_THROW(invalid = Meta::readCached(buffer));
    ASSERT_NE(invalid, nullptr);
    EXPECT_THROW(invalid->bytes(), std::logic_error);
    EXPECT_THROW(invalid->bytes(), std::logic_error);
    EXPECT_THROW(invalid->view(), std::logic_error);

    Meta::mutate<bool>(buffer, [](auto &results) {
        results = Results{};
        return true;
    });
    const auto empty = decode(Meta::readCached(buffer)->bytes().get());
    EXPECT_TRUE(empty.valid());
    EXPECT_TRUE(empty.empty());
    EXPECT_FALSE(source.expired()); // Failed serialization must retain its retry input.
    EXPECT_THROW(invalid->bytes(), std::logic_error);
    invalid.reset();
    EXPECT_TRUE(source.expired());
}

TEST_F(FrameResultsPacketTest, MissingMetadataIsNotAnEmptyEnvelope) {
    g_autoptr(GstBuffer) missing = gst_buffer_new();
    EXPECT_EQ(Meta::readCached(missing), nullptr);
    ASSERT_NE(Meta::add(missing), nullptr);
    EXPECT_TRUE(decode(Meta::readCached(missing)->bytes().get()).valid());
}

TEST_F(FrameResultsPacketTest, ReplacingMetadataOnSameBufferDoesNotReuseOldPacket) {
    const auto oldPacket = Meta::readCached(buffer)->bytes();
    ASSERT_TRUE(Meta::remove(buffer));
    ASSERT_NE(Meta::add(buffer), nullptr);
    const auto newPacket = Meta::readCached(buffer)->bytes();
    EXPECT_NE(oldPacket.get(), newPacket.get());
    EXPECT_EQ(decode(oldPacket.get()).count<Detection>(), 1U);
    EXPECT_TRUE(decode(newPacket.get()).empty());
}

TEST_F(FrameResultsPacketTest, AttachingAValueDoesNotKeepTheCallersMutableContainer) {
    Results supplied;
    supplied.add(Detection{});
    g_autoptr(GstBuffer) attached = gst_buffer_new();
    ASSERT_NE(Meta::add(attached, supplied), nullptr);
    const auto packet = Meta::readCached(attached)->bytes();
    supplied.add(Detection{});
    EXPECT_EQ(supplied.count<Detection>(), 2U);
    EXPECT_EQ(Meta::read(attached)->count<Detection>(), 1U);
    EXPECT_EQ(Meta::readCached(attached)->bytes(), packet);
    EXPECT_EQ(decode(packet.get()).count<Detection>(), 1U);
}

} // namespace
