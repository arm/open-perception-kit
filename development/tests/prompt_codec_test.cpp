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

#include "opk/BoxPromptCodec.h"
#include "opk/BoxPromptInputs.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace {

using namespace open_perception_kit::metadata;
constexpr uint64_t RequestId = (uint64_t{1} << 63) + 7;
constexpr uint64_t Revision = 3;

BoxPromptInputsT request() {
    BoxPromptInputsT result;
    result.request = std::make_unique<RequestEnvelopeT>();
    result.request->request_id = RequestId;
    result.request->revision = Revision;
    result.request->destination = std::make_unique<RequestDestinationT>();
    result.request->destination->inference_instance = "sam";
    result.request->destination->source_name = "pipelineVideoFrame";
    result.payload = std::make_unique<BoxPromptPayloadT>();
    result.payload->box = std::make_unique<PromptBoxT>();
    result.payload->box->right = 1.0;
    result.payload->box->bottom = 1.0;
    return result;
}

std::vector<uint8_t> packet(const BoxPromptInputsT &input) {
    flatbuffers::FlatBufferBuilder builder;
    const auto root = BoxPromptInputs::Pack(builder, &input);
    FinishBoxPromptInputsBuffer(builder, root);
    return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

TEST(PromptCodec, BoxCompositionDecodesAndRevalidatesThroughGenericInterface) {
    const opk::PromptCodec &codec = opk::boxPromptCodec();
    const auto original = request();
    const auto bytes = packet(original);
    uint64_t id = 0;
    uint64_t revision = 0;
    const auto decoded = codec.decode(bytes, "session", "sam", "pipelineVideoFrame", id, revision);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(id, RequestId);
    EXPECT_EQ(revision, Revision);
    EXPECT_TRUE(codec.validate(*decoded, "sam", "pipelineVideoFrame"));
    EXPECT_FALSE(codec.validate(*decoded, "other", "pipelineVideoFrame"));
    EXPECT_FALSE(codec.validate(*decoded, "sam", "other"));
}

TEST(PromptCodec, SemanticallyRejectedRequestsRetainDecodedAcknowledgementIdentity) {
    const opk::PromptCodec &codec = opk::boxPromptCodec();
    for (const bool badVersion : {false, true}) {
        auto input = request();
        if (badVersion) {
            input.schema_major = 2;
        } else {
            input.payload->box->left = 1.0;
        }
        const auto bytes = packet(input);
        uint64_t id = 0;
        uint64_t revision = 0;
        EXPECT_FALSE(codec.decode(bytes, "session", "sam", "pipelineVideoFrame", id, revision));
        EXPECT_EQ(id, RequestId);
        EXPECT_EQ(revision, Revision);
    }
    const auto original = request();
    const auto bytes = packet(original);
    uint64_t id = 0;
    uint64_t revision = 0;
    EXPECT_FALSE(codec.decode(bytes, "session", "other", "pipelineVideoFrame", id, revision));
    EXPECT_EQ(id, RequestId);
    EXPECT_EQ(revision, Revision);
}

TEST(PromptCodec, MalformedAndOversizedPacketsDoNotInventAcknowledgementIdentity) {
    const opk::PromptCodec &codec = opk::boxPromptCodec();
    uint64_t id = 0;
    uint64_t revision = 0;
    const std::array<uint8_t, 3> malformed{};
    EXPECT_FALSE(codec.decode(malformed, "session", "sam", "pipelineVideoFrame", id, revision));
    const std::array<uint8_t, opk::MaxBoxPromptRequestBytes + 1> oversized{};
    EXPECT_FALSE(codec.decode(oversized, "session", "sam", "pipelineVideoFrame", id, revision));
    EXPECT_EQ(id, 0U);
    EXPECT_EQ(revision, 0U);
}

TEST(PromptCodec, AbsentPayloadRemainsAValidNoPromptState) {
    auto input = request();
    input.payload.reset();
    const auto bytes = packet(input);
    const opk::PromptCodec &codec = opk::boxPromptCodec();
    uint64_t id = 0;
    uint64_t revision = 0;
    const auto decoded = codec.decode(bytes, "session", "sam", "pipelineVideoFrame", id, revision);
    ASSERT_TRUE(decoded);
    EXPECT_FALSE(decoded->payload);
    EXPECT_TRUE(codec.validate(*decoded, "sam", "pipelineVideoFrame"));
    EXPECT_EQ(id, RequestId);
    EXPECT_EQ(revision, Revision);
}

} // namespace
