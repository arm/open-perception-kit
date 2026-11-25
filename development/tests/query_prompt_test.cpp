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

#include "opk/PromptCodecs.h"
#include "opk/QueryPromptInputs.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <limits>

namespace {
using namespace open_perception_kit::metadata;

RequestEnvelopeT envelope() {
    RequestEnvelopeT result;
    result.request_id = UINT64_MAX;
    result.revision = UINT64_MAX;
    result.destination = std::make_unique<RequestDestinationT>();
    result.destination->inference_instance = "query";
    result.destination->source_name = "video";
    return result;
}

TextPromptInputsT textRequest() {
    TextPromptInputsT result;
    result.request = std::make_unique<RequestEnvelopeT>(envelope());
    result.payload = std::make_unique<TextPromptPayloadT>();
    auto group = std::make_unique<TextPromptGroupT>();
    group->group_id = "person";
    group->description = "a person";
    result.payload->groups.push_back(std::move(group));
    return result;
}

VisualPromptInputsT visualRequest() {
    VisualPromptInputsT result;
    result.request = std::make_unique<RequestEnvelopeT>(envelope());
    result.payload = std::make_unique<VisualPromptPayloadT>();
    auto reference = std::make_unique<PromptReferenceImageT>();
    reference->width = 2;
    reference->height = 1;
    reference->encoding = PromptReferenceEncoding::RGB8;
    reference->pixels = {1, 2, 3, 4, 5, 6};
    result.payload->references.push_back(std::move(reference));
    auto group = std::make_unique<VisualPromptGroupT>();
    group->group_id = "sample";
    group->box = std::make_unique<VisualPromptBoxT>();
    group->box->right = 1;
    group->box->bottom = 1;
    result.payload->groups.push_back(std::move(group));
    return result;
}

std::vector<uint8_t> packet(const TextPromptInputsT &request) {
    flatbuffers::FlatBufferBuilder builder;
    FinishTextPromptInputsBuffer(builder, TextPromptInputs::Pack(builder, &request));
    return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<uint8_t> packet(const VisualPromptInputsT &request) {
    flatbuffers::FlatBufferBuilder builder;
    FinishVisualPromptInputsBuffer(builder, VisualPromptInputs::Pack(builder, &request));
    return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

opk::Result<opk::PromptSnapshot> decode(const TextPromptInputsT &request) {
    uint64_t id = 0;
    uint64_t revision = 0;
    return opk::decodeTextPrompt(packet(request), "session", "query", "video", id, revision);
}

opk::Result<opk::PromptSnapshot> decode(const VisualPromptInputsT &request) {
    uint64_t id = 0;
    uint64_t revision = 0;
    return opk::decodeVisualPrompt(packet(request), "session", "query", "video", id, revision);
}

TEST(QueryPrompt, CodecRegistryExposesDistinctLimitsAndDecodesBothFormats) {
    const auto *const box = opk::findPromptCodec("BPRM");
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->maxSerializedBytes, 4096);
    for (const auto &bytes : {packet(textRequest()), packet(visualRequest())}) {
        const auto *const registration = opk::findPromptCodec(std::span<const uint8_t>{bytes});
        ASSERT_NE(registration, nullptr);
        EXPECT_EQ(registration->limits.maxGroups, opk::MaxQueryPromptGroups);
        uint64_t id = 0;
        uint64_t revision = 0;
        const auto snapshot =
            registration->codec->decode(bytes, "session", "query", "video", id, revision);
        ASSERT_TRUE(snapshot);
        EXPECT_EQ(id, UINT64_MAX);
        EXPECT_TRUE(registration->codec->validate(*snapshot, "query", "video"));
    }
    EXPECT_EQ(opk::findPromptCodec("NOPE"), nullptr);
    EXPECT_EQ(opk::findPromptCodec(std::span<const uint8_t>{}), nullptr);
}

TEST(QueryPrompt, OwnedReferenceSurvivesCallerMutationAndSupportsSharedReferences) {
    auto request = visualRequest();
    request.payload->groups.push_back(
        std::make_unique<VisualPromptGroupT>(*request.payload->groups[0]));
    request.payload->groups[1]->group_id = "second";
    auto bytes = packet(request);
    uint64_t id = 0;
    uint64_t revision = 0;
    const auto snapshot = opk::decodeVisualPrompt(bytes, "session", "query", "video", id, revision);
    ASSERT_TRUE(snapshot);
    EXPECT_EQ(id, UINT64_MAX);
    EXPECT_EQ(revision, UINT64_MAX);
    std::fill(bytes.begin(), bytes.end(), 0);
    bytes.clear();
    const auto owner = std::dynamic_pointer_cast<const opk::VisualPromptPayload>(snapshot->payload);
    ASSERT_TRUE(owner);
    EXPECT_EQ(owner->value().references()->Get(0)->pixels()->Get(5), 6);
    EXPECT_EQ(owner->value().groups()->size(), 2);
    EXPECT_TRUE(opk::validateVisualPrompt(*snapshot, "query", "video"));
    EXPECT_FALSE(opk::validateVisualPrompt(*snapshot, "other", "video"));
    EXPECT_FALSE(opk::validateTextPrompt(*snapshot, "query", "video"));
}

TEST(QueryPrompt, AbsentIsValidButPresentEmptyIsNot) {
    auto text = textRequest();
    text.payload.reset();
    const auto absent = decode(text);
    ASSERT_TRUE(absent);
    EXPECT_FALSE(absent->payload);
    text.payload = std::make_unique<TextPromptPayloadT>();
    EXPECT_FALSE(decode(text));
    auto visual = visualRequest();
    visual.payload.reset();
    EXPECT_TRUE(decode(visual));
    visual.payload = std::make_unique<VisualPromptPayloadT>();
    EXPECT_FALSE(decode(visual));
}

TEST(QueryPrompt, RejectsMalformedTextIdentityVersionAndBounds) {
    for (const std::string &invalid : {std::string{},
                                       std::string("\xc0\xaf"),
                                       std::string("a\0b", 3),
                                       std::string(opk::MaxPromptDescriptionBytes + 1, 'x'),
                                       std::string(" \t\n")}) {
        auto request = textRequest();
        request.payload->groups[0]->description = invalid;
        EXPECT_FALSE(decode(request));
    }
    for (const std::string &invalid : {std::string{},
                                       std::string("\xff"),
                                       std::string("a\0b", 3),
                                       std::string(opk::MaxPromptGroupNameBytes + 1, 'x')}) {
        auto request = textRequest();
        request.payload->groups[0]->group_id = invalid;
        EXPECT_FALSE(decode(request));
    }
    auto request = textRequest();
    request.payload->groups.push_back(
        std::make_unique<TextPromptGroupT>(*request.payload->groups[0]));
    EXPECT_FALSE(decode(request));
    request = textRequest();
    for (size_t i = 1; i <= opk::MaxQueryPromptGroups; ++i) {
        auto group = std::make_unique<TextPromptGroupT>(*request.payload->groups[0]);
        group->group_id = std::to_string(i);
        request.payload->groups.push_back(std::move(group));
    }
    EXPECT_FALSE(decode(request));
    request = textRequest();
    request.schema_minor = 1;
    EXPECT_FALSE(decode(request));
    request = textRequest();
    request.request.reset();
    EXPECT_FALSE(decode(request));
    request = textRequest();
    request.request->request_id = 0;
    EXPECT_FALSE(decode(request));
    request = textRequest();
    request.request->destination->source_name = "other";
    EXPECT_FALSE(decode(request));
}

TEST(QueryPrompt, RejectsInvalidReferenceEncodingLengthsGeometryAndIndices) {
    auto request = visualRequest();
    request.payload->references[0]->width = UINT32_MAX;
    EXPECT_FALSE(decode(request));
    request = visualRequest();
    request.payload->references[0]->pixels.pop_back();
    EXPECT_FALSE(decode(request));
    request = visualRequest();
    request.payload->references[0]->encoding = PromptReferenceEncoding::Unknown;
    EXPECT_FALSE(decode(request));
    request = visualRequest();
    request.payload->groups[0]->reference_index = 1;
    EXPECT_FALSE(decode(request));
    request = visualRequest();
    request.payload->groups[0]->box.reset();
    EXPECT_FALSE(decode(request));
    for (const double invalid : {-0.1,
                                 1.0,
                                 std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::infinity()}) {
        request = visualRequest();
        request.payload->groups[0]->box->left = invalid;
        EXPECT_FALSE(decode(request));
    }
}

TEST(QueryPrompt, AliasedReferencesCannotBypassAggregateAllocationLimit) {
    flatbuffers::FlatBufferBuilder builder;
    const std::vector<uint8_t> pixels(2048 * 2048 * 3, 0);
    const auto bytes = builder.CreateVector(pixels);
    const auto reference =
        CreatePromptReferenceImage(builder, 2048, 2048, PromptReferenceEncoding::RGB8, bytes);
    const auto references = builder.CreateVector(std::vector{reference, reference});
    const auto box = CreateVisualPromptBox(builder, 0, 0, 1, 1);
    const auto group = CreateVisualPromptGroup(builder, builder.CreateString("sample"), 0, 0, box);
    const auto groups = builder.CreateVector(std::vector{group});
    const auto payload = CreateVisualPromptPayload(builder, references, groups);
    const auto identity = envelope();
    const auto request = RequestEnvelope::Pack(builder, &identity);
    FinishVisualPromptInputsBuffer(builder,
                                   CreateVisualPromptInputs(builder, 1, 0, request, payload));
    ASSERT_LT(builder.GetSize(), opk::MaxVisualPromptRequestBytes);
    uint64_t id = 0;
    uint64_t revision = 0;
    EXPECT_FALSE(opk::decodeVisualPrompt({builder.GetBufferPointer(), builder.GetSize()},
                                         "session",
                                         "query",
                                         "video",
                                         id,
                                         revision));
    EXPECT_EQ(id, UINT64_MAX);
}

TEST(QueryPrompt, RejectsReferenceCountBeforeOwningAliasedTables) {
    flatbuffers::FlatBufferBuilder builder;
    const auto pixels = builder.CreateVector(std::vector<uint8_t>{1, 2, 3});
    const auto reference =
        CreatePromptReferenceImage(builder, 1, 1, PromptReferenceEncoding::RGB8, pixels);
    const auto references =
        builder.CreateVector(std::vector(opk::MaxPromptReferences + 1, reference));
    const auto box = CreateVisualPromptBox(builder, 0, 0, 1, 1);
    const auto group = CreateVisualPromptGroup(builder, builder.CreateString("sample"), 0, 0, box);
    const auto groups = builder.CreateVector(std::vector{group});
    const auto payload = CreateVisualPromptPayload(builder, references, groups);
    const auto identity = envelope();
    const auto request = RequestEnvelope::Pack(builder, &identity);
    FinishVisualPromptInputsBuffer(builder,
                                   CreateVisualPromptInputs(builder, 1, 0, request, payload));
    uint64_t id = 0;
    uint64_t revision = 0;
    EXPECT_FALSE(opk::decodeVisualPrompt({builder.GetBufferPointer(), builder.GetSize()},
                                         "session",
                                         "query",
                                         "video",
                                         id,
                                         revision));
}

TEST(QueryPrompt, TextViewsPreserveUnicodeAndRevalidateBinding) {
    auto request = textRequest();
    request.payload->groups[0]->description = "piros autó";
    const auto snapshot = decode(request);
    ASSERT_TRUE(snapshot);
    EXPECT_TRUE(opk::validateTextPrompt(*snapshot, "query", "video"));
    EXPECT_FALSE(opk::validateTextPrompt(*snapshot, "query", "other"));
    const auto owner = std::dynamic_pointer_cast<const opk::TextPromptPayload>(snapshot->payload);
    ASSERT_TRUE(owner);
    EXPECT_EQ(owner->value().groups()->Get(0)->description()->str(), "piros autó");
}

TEST(QueryPrompt, WireValidationRejectsTruncatedAndWrongFormat) {
    auto bytes = packet(textRequest());
    uint64_t id = 0;
    uint64_t revision = 0;
    EXPECT_FALSE(opk::decodeVisualPrompt(bytes, "session", "query", "video", id, revision));
    bytes.resize(bytes.size() / 2);
    EXPECT_FALSE(opk::decodeTextPrompt(bytes, "session", "query", "video", id, revision));
}
} // namespace
