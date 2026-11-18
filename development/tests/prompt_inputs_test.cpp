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

#include "gst/PromptInputsMeta.h"
#include "opk/BoxPromptInputs.h"

#include <array>
#include <gtest/gtest.h>
#include <limits>
#include <thread>
#include <type_traits>
#include <utility>

namespace {
using namespace open_perception_kit::metadata;

template <class T>
concept HasPublicRequest = requires(T &snapshot) { snapshot.request; };

template <class T>
concept HasWritableDestination =
    requires(const T &snapshot) { snapshot.destination()->source_name = std::string{}; };

static_assert(!HasPublicRequest<opk::PromptSnapshot>);
static_assert(!HasWritableDestination<opk::PromptSnapshot>);
static_assert(std::is_same_v<decltype(std::declval<const opk::PromptSnapshot &>().destination()),
                             const RequestDestinationT *>);
static_assert(std::is_same_v<decltype(std::declval<const opk::PromptSnapshot &>().copyRequest()),
                             RequestEnvelopeT>);

BoxPromptInputsT request(const uint64_t id = 1, const uint64_t revision = 1) {
    BoxPromptInputsT result;
    result.request = std::make_unique<RequestEnvelopeT>();
    result.request->request_id = id;
    result.request->revision = revision;
    result.request->destination = std::make_unique<RequestDestinationT>();
    result.request->destination->inference_instance = "sam";
    result.request->destination->source_name = "pipelineVideoFrame";
    result.payload = std::make_unique<BoxPromptPayloadT>();
    result.payload->box = std::make_unique<PromptBoxT>();
    result.payload->box->right = 1.0;
    result.payload->box->bottom = 1.0;
    return result;
}

opk::Result<void> submit(opk::PromptController &controller,
                         const std::string &session,
                         BoxPromptInputsT request,
                         const std::string &inferenceInstance,
                         const std::string &sourceName) {
    auto snapshot =
        opk::makeBoxPromptSnapshot(session, std::move(request), inferenceInstance, sourceName);
    if (!snapshot) {
        return tl::unexpected(snapshot.error());
    }
    return controller.submit(std::move(*snapshot));
}

class OwnedTextPayload final : public opk::PromptPayload {
  public:
    explicit OwnedTextPayload(std::string text) : text(std::move(text)) {}
    opk::PromptFormat format() const override {
        return {"TEST", 1, 0};
    }

    const std::string text;
};

TEST(PromptInputs, DetachedEnvelopeCopiesCannotChangePublishedIdentityOrDestination) {
    opk::PromptController controller;
    ASSERT_TRUE(submit(controller, "one", request(), "sam", "pipelineVideoFrame"));
    const auto snapshot = controller.snapshot();
    ASSERT_TRUE(snapshot);
    auto copy = snapshot->copyRequest();
    ASSERT_TRUE(copy.destination);
    copy.request_id = 9;
    copy.revision = 99;
    copy.destination->inference_instance = "other";
    copy.destination->source_name = "other";
    EXPECT_EQ(snapshot->requestId(), 1U);
    EXPECT_EQ(snapshot->revision(), 1U);
    EXPECT_EQ(snapshot->application(), RequestApplication::UntilReplaced);
    ASSERT_NE(snapshot->destination(), nullptr);
    EXPECT_EQ(snapshot->destination()->inference_instance, "sam");
    EXPECT_EQ(snapshot->destination()->source_name, "pipelineVideoFrame");
    EXPECT_TRUE(controller.cancel("one", 1, 1));
}

TEST(PromptInputs, GenericPayloadOwnsDataAcrossReplacementSessionEndAndBufferCopies) {
    gst_init(nullptr, nullptr);
    opk::PromptController controller;
    auto wire = request();
    std::string source = "find the red car";
    opk::PromptSnapshot first{
        "one", std::move(*wire.request), std::make_shared<const OwnedTextPayload>(source)};
    source.clear();
    ASSERT_TRUE(controller.submit(std::move(first)));
    auto snapshot = controller.snapshot();
    ASSERT_TRUE(snapshot);
    EXPECT_EQ(snapshot->payload->format(), (opk::PromptFormat{"TEST", 1, 0}));
    EXPECT_EQ(dynamic_cast<const OwnedTextPayload &>(*snapshot->payload).text, "find the red car");
    const std::weak_ptr<const opk::PromptPayload> lifetime = snapshot->payload;
    GstBuffer *const buffer = gst_buffer_new();
    ASSERT_NE(opk::PromptInputsMeta::add(buffer, snapshot, "infer", 2, 2), nullptr);
    GstBuffer *const copy = gst_buffer_copy(buffer);
    ASSERT_NE(copy, nullptr);
    wire = request(1, 2);
    ASSERT_TRUE(controller.submit({"one",
                                   std::move(*wire.request),
                                   std::make_shared<const OwnedTextPayload>("find the blue car")}));
    ASSERT_TRUE(controller.endSession("one"));
    snapshot.reset();
    gst_buffer_unref(buffer);
    EXPECT_FALSE(lifetime.expired());
    const auto *const meta = opk::PromptInputsMeta::get(copy);
    ASSERT_NE(meta, nullptr);
    EXPECT_EQ(meta->snapshot->revision(), 1U);
    EXPECT_EQ(dynamic_cast<const OwnedTextPayload &>(*meta->snapshot->payload).text,
              "find the red car");
    gst_buffer_unref(copy);
    EXPECT_TRUE(lifetime.expired());
}

TEST(PromptInputs, GenericControllerRejectsInvalidEnvelopeWithoutReplacingActivePayload) {
    opk::PromptController controller;
    auto wire = request();
    ASSERT_TRUE(controller.submit(
        {"one", *wire.request, std::make_shared<const OwnedTextPayload>("original")}));
    const auto original = controller.snapshot();
    for (const bool missingDestination : {false, true}) {
        auto invalid = request(1, 2);
        if (missingDestination) {
            invalid.request->destination.reset();
        } else {
            invalid.request->request_id = 0;
        }
        EXPECT_FALSE(controller.submit({"one", std::move(*invalid.request), original->payload}));
        EXPECT_EQ(controller.snapshot(), original);
    }
}

TEST(PromptInputs, RejectsInvalidUpdatesAndKeepsPreviousSnapshot) {
    opk::PromptController controller;
    ASSERT_TRUE(submit(controller, "one", request(), "sam", "pipelineVideoFrame"));
    const auto old = controller.snapshot();
    auto malformed = request(1, 2);
    malformed.payload->box->left = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(submit(controller, "one", std::move(malformed), "sam", "pipelineVideoFrame"));
    EXPECT_EQ(old, controller.snapshot());
    EXPECT_FALSE(submit(controller, "two", request(), "sam", "pipelineVideoFrame"));
    EXPECT_FALSE(submit(controller, "one", request(), "sam", "pipelineVideoFrame"));
    EXPECT_FALSE(submit(controller, "one", request(2, 2), "sam", "pipelineVideoFrame"));
    EXPECT_FALSE(submit(controller, "one", request(0), "sam", "pipelineVideoFrame"));
    EXPECT_FALSE(submit(controller, "one", request(2), "other", "pipelineVideoFrame"));
    auto rerouted = request(1, 2);
    rerouted.request->destination->inference_instance = "other";
    EXPECT_FALSE(submit(controller, "one", std::move(rerouted), "other", "pipelineVideoFrame"));
    ASSERT_TRUE(submit(controller, "one", request(1, 2), "sam", "pipelineVideoFrame"));
    EXPECT_EQ(old->revision(), 1);
    EXPECT_EQ(controller.snapshot()->revision(), 2);
    EXPECT_FALSE(controller.cancel("one", 1, 1));
    EXPECT_FALSE(controller.endSession("two"));
    ASSERT_TRUE(controller.cancel("one", 1, 2));
    EXPECT_FALSE(controller.snapshot());
    EXPECT_FALSE(submit(controller, "one", request(1, 3), "sam", "pipelineVideoFrame"));
    EXPECT_EQ(old->revision(), 1);
}

TEST(PromptInputs, SessionHandoverRetainsOldNamespaceAndRejectsLateCommands) {
    opk::PromptController controller;
    constexpr uint64_t highId = (uint64_t{1} << 63) + 123;
    ASSERT_TRUE(submit(controller, "one", request(highId), "sam", "pipelineVideoFrame"));
    const auto inFlight = controller.snapshot();
    ASSERT_TRUE(controller.endSession("one"));
    EXPECT_FALSE(submit(controller, "one", request(2), "sam", "pipelineVideoFrame"));
    ASSERT_TRUE(submit(controller, "two", request(highId), "sam", "pipelineVideoFrame"));
    EXPECT_EQ(inFlight->sessionId, "one");
    EXPECT_EQ(controller.snapshot()->sessionId, "two");
    EXPECT_EQ(inFlight->requestId(), highId);
    EXPECT_FALSE(controller.cancel("one", highId, 1));
    EXPECT_TRUE(controller.snapshot());
}

TEST(PromptInputs, AbsentPayloadIsAcceptedWithoutInventingSnapshot) {
    opk::PromptController controller;
    ASSERT_TRUE(submit(controller, "one", request(), "sam", "pipelineVideoFrame"));
    auto absent = request(1, 2);
    absent.payload.reset();
    ASSERT_TRUE(submit(controller, "one", std::move(absent), "sam", "pipelineVideoFrame"));
    EXPECT_FALSE(controller.snapshot());
    EXPECT_TRUE(controller.cancel("one", 1, 2));
    auto malformed = request(2);
    malformed.payload->box.reset();
    EXPECT_FALSE(submit(controller, "one", std::move(malformed), "sam", "pipelineVideoFrame"));
}

TEST(PromptInputs, RetainedRevisionsAreBoundedAndReleaseAllowsUpdates) {
    opk::PromptController controller;
    std::vector<std::shared_ptr<const opk::PromptSnapshot>> retained;
    for (size_t revision = 1; revision <= opk::PromptController::MaxRetainedRevisions; ++revision) {
        ASSERT_TRUE(submit(controller, "one", request(1, revision), "sam", "pipelineVideoFrame"));
        retained.push_back(controller.snapshot());
    }
    EXPECT_FALSE(submit(controller, "one", request(1, 9), "sam", "pipelineVideoFrame"));
    retained.erase(retained.begin());
    EXPECT_TRUE(submit(controller, "one", request(1, 9), "sam", "pipelineVideoFrame"));
    EXPECT_EQ(retained.back()->revision(), 8);
}

TEST(PromptInputs, SnapshotReadersNeverObservePartiallyPublishedUpdates) {
    opk::PromptController controller;
    ASSERT_TRUE(submit(controller, "one", request(), "sam", "pipelineVideoFrame"));
    std::thread updates([&] {
        for (uint64_t revision = 2; revision <= 100; ++revision) {
            EXPECT_TRUE(
                submit(controller, "one", request(1, revision), "sam", "pipelineVideoFrame"));
        }
    });
    for (size_t iteration = 0; iteration < 100; ++iteration) {
        const auto snapshot = controller.snapshot();
        EXPECT_TRUE(snapshot);
        if (!snapshot) {
            continue;
        }
        EXPECT_EQ(snapshot->sessionId, "one");
        EXPECT_EQ(snapshot->requestId(), 1);
        EXPECT_EQ(dynamic_cast<const opk::BoxPromptPayload &>(*snapshot->payload).box.right, 1.0);
    }
    updates.join();
    EXPECT_EQ(controller.snapshot()->revision(), 100);
}

TEST(PromptInputs, RequestAndSessionHistoryHaveExplicitBounds) {
    opk::PromptController controller;
    for (size_t id = 1; id <= opk::PromptController::MaxRequestsPerSession; ++id) {
        ASSERT_TRUE(submit(controller, "one", request(id), "sam", "pipelineVideoFrame"));
    }
    EXPECT_FALSE(submit(controller,
                        "one",
                        request(opk::PromptController::MaxRequestsPerSession + 1),
                        "sam",
                        "pipelineVideoFrame"));
    ASSERT_TRUE(controller.endSession("one"));
    for (size_t id = 1; id < opk::PromptController::MaxEndedSessions; ++id) {
        const std::string session = "next-" + std::to_string(id);
        ASSERT_TRUE(submit(controller, session, request(), "sam", "pipelineVideoFrame"));
        ASSERT_TRUE(controller.endSession(session));
    }
    EXPECT_FALSE(submit(controller, "over-limit", request(), "sam", "pipelineVideoFrame"));
}

TEST(PromptInputs, DecodeValidatesIdentifierSizeAndAll64Bits) {
    flatbuffers::FlatBufferBuilder builder;
    const auto original = request(std::numeric_limits<uint64_t>::max());
    FinishBoxPromptInputsBuffer(builder, BoxPromptInputs::Pack(builder, &original));
    const auto decoded = opk::decodeBoxPrompt({builder.GetBufferPointer(), builder.GetSize()});
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->request->request_id, std::numeric_limits<uint64_t>::max());
    EXPECT_FALSE(opk::decodeBoxPrompt({builder.GetBufferPointer(), 3}));
    const std::array<uint8_t, opk::MaxBoxPromptRequestBytes + 1> oversized{};
    EXPECT_FALSE(opk::decodeBoxPrompt(oversized));
}

TEST(PromptMeta, WholeCopiesShareOwnershipAndRegionCopiesRejectGeometry) {
    gst_init(nullptr, nullptr);
    opk::PromptController controller;
    ASSERT_TRUE(submit(controller, "one", request(), "sam", "pipelineVideoFrame"));
    auto snapshot = controller.snapshot();
    const std::weak_ptr<const opk::PromptSnapshot> lifetime = snapshot;
    GstBuffer *const original = gst_buffer_new_allocate(nullptr, 16, nullptr);
    ASSERT_NE(opk::PromptInputsMeta::add(original, snapshot, "infer", 2, 2), nullptr);
    GstBuffer *const copied = gst_buffer_copy(original);
    ASSERT_NE(copied, nullptr);
    const auto *const meta = opk::PromptInputsMeta::get(copied);
    ASSERT_NE(meta, nullptr);
    EXPECT_EQ(meta->snapshot, snapshot);
    EXPECT_EQ(meta->targetElement, "infer");
    GstBuffer *const region = gst_buffer_copy_region(original, GST_BUFFER_COPY_ALL, 0, 8);
    EXPECT_EQ(opk::PromptInputsMeta::get(region), nullptr);
    ASSERT_TRUE(controller.endSession("one"));
    snapshot.reset();
    gst_buffer_unref(original);
    EXPECT_FALSE(lifetime.expired());
    gst_buffer_unref(copied);
    EXPECT_TRUE(lifetime.expired());
    gst_buffer_unref(region);
}

TEST(PromptMeta, NonWritableAttachmentAndDuplicateMetaAreRejected) {
    gst_init(nullptr, nullptr);
    opk::PromptController controller;
    ASSERT_TRUE(submit(controller, "one", request(), "sam", "pipelineVideoFrame"));
    GstBuffer *const buffer = gst_buffer_new();
    gst_buffer_ref(buffer);
    EXPECT_EQ(opk::PromptInputsMeta::add(buffer, controller.snapshot(), "infer", 2, 2), nullptr);
    gst_buffer_unref(buffer);
    EXPECT_NE(opk::PromptInputsMeta::add(buffer, controller.snapshot(), "infer", 2, 2), nullptr);
    EXPECT_EQ(opk::PromptInputsMeta::add(buffer, controller.snapshot(), "infer", 2, 2), nullptr);
    gst_buffer_unref(buffer);
}

} // namespace
