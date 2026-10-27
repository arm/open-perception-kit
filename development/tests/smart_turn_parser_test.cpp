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

#include "postproc/SmartTurnParser.h"
#include <gtest/gtest.h>
#include <limits>

namespace {
using Payload = open_perception_kit::metadata::TurnDetectionsT;
struct Fixture {
    opk::stdop::postproc::SmartTurnParser parser;
    opk::AttributeMap attributes;
    float probability = 0.5f;
    opk::TensorView tensor{
        &probability, sizeof(float), opk::Shape(1, 1), opk::Dtype::Float32, 1, 0};
    opk::TensorParser::Input input{attributes};
    open_perception_kit::FrameResults results;
    Fixture() {
        input.tensors[0] = &tensor;
        input.inferenceInfo.audio.hopSampleCount = 512;
        input.inferenceInfo.audio.windowStartSample = 512;
        input.inferenceInfo.audio.windowEndSample = 128512;
        input.inferenceInfo.modelName = "smart-turn";
        input.inferenceInfo.inferElementId = "audio0";
        input.inferenceInfo.parentId = 42;
        input.producerInfo.instance_id = "audio0/turn-post";
        input.producerInfo.component = "opk-std-ops/GenericPostprocess";
        input.producerInfo.implementation = "SmartTurnParser";
    }
};

TEST(SmartTurnParser, PublishesEveryAssessmentWithoutPrintingOrReplacingVad) {
    Fixture f;
    f.results.add(open_perception_kit::metadata::VoiceActivityDetectionsT{});
    testing::internal::CaptureStdout();
    const auto first = f.parser.parse(f.input, f.results);
    const auto retry = f.parser.parse(f.input, f.results);
    f.attributes.set("threshold", 0.6);
    const auto second = f.parser.parse(f.input, f.results);
    const auto output = testing::internal::GetCapturedStdout();
    ASSERT_TRUE(first);
    ASSERT_TRUE(retry);
    ASSERT_TRUE(second);
    EXPECT_TRUE(output.empty());
    EXPECT_EQ(f.parser.getProvidedContentTypes(), std::vector<std::string_view>{"turnDetection"});

    const auto bytes = open_perception_kit::serialize(f.results);
    open_perception_kit::FrameResults decoded{std::span<const uint8_t>(bytes)};
    size_t count = 0;
    decoded.for_each<Payload>([&](const auto &payload) {
        EXPECT_EQ(payload.schema_major, 1);
        EXPECT_EQ(payload.schema_minor, 0);
        ASSERT_TRUE(payload.layer);
        EXPECT_EQ(payload.layer->content_type, "turnDetection");
        EXPECT_EQ(payload.layer->model, "smart-turn");
        EXPECT_EQ(payload.layer->infer_element_id, "audio0");
        ASSERT_TRUE(payload.layer->producer);
        EXPECT_EQ(payload.layer->producer->instance_id, "audio0/turn-post");
        EXPECT_EQ(payload.layer->producer->component, "opk-std-ops/GenericPostprocess");
        EXPECT_EQ(payload.layer->producer->implementation, "SmartTurnParser");
        ASSERT_EQ(payload.detections.size(), 1u);
        ASSERT_TRUE(payload.detections[0]);
        const auto &detection = *payload.detections[0];
        ASSERT_TRUE(detection.object);
        EXPECT_EQ(detection.object->parent_id, 42u);
        EXPECT_FLOAT_EQ(detection.turn_probability, 0.5f);
        EXPECT_EQ(detection.turn_complete, count < 2); // Equality passes; no completion latch.
        EXPECT_EQ(detection.window_start_sample, 512u);
        EXPECT_EQ(detection.window_end_sample, 128512u);
        ++count;
    });
    EXPECT_EQ(count, 3u);
    EXPECT_TRUE(decoded.get<open_perception_kit::metadata::VoiceActivityDetectionsT>().has_value());
}

TEST(SmartTurnParser, RoundTripsZeroAndAbsentTimestampsAndDiscontinuity) {
    Fixture f;
    auto &audio = f.input.inferenceInfo.audio;
    audio.windowStartSample = 0;
    audio.windowEndSample = 16000; // Short real history; synthetic padding is excluded.
    audio.windowStartTimestampNs = 0;
    audio.windowEndTimestampNs = 1000000000;
    audio.discontinuity = true;
    ASSERT_TRUE(f.parser.parse(f.input, f.results));
    audio.windowStartTimestampNs.reset();
    audio.windowEndTimestampNs.reset();
    audio.discontinuity = false;
    ASSERT_TRUE(f.parser.parse(f.input, f.results));
    const auto bytes = open_perception_kit::serialize(f.results);
    open_perception_kit::FrameResults decoded{std::span<const uint8_t>(bytes)};
    size_t count = 0;
    decoded.for_each<Payload>([&](const auto &payload) {
        const auto &detection = *payload.detections[0];
        EXPECT_EQ(detection.window_start_sample, 0u);
        EXPECT_EQ(detection.window_end_sample, 16000u);
        if (count++ == 0) {
            ASSERT_TRUE(detection.window_start_ts_ns);
            ASSERT_TRUE(detection.window_end_ts_ns);
            EXPECT_EQ(*detection.window_start_ts_ns, 0u);
            EXPECT_EQ(*detection.window_end_ts_ns, 1000000000u);
            EXPECT_TRUE(detection.discontinuity);
        } else {
            EXPECT_FALSE(detection.window_start_ts_ns);
            EXPECT_FALSE(detection.window_end_ts_ns);
            EXPECT_FALSE(detection.discontinuity);
        }
    });
    EXPECT_EQ(count, 2u);
}

TEST(SmartTurnParser, RejectsInvalidProbabilitiesThresholdsAndTensors) {
    Fixture f;
    const auto before = open_perception_kit::serialize(f.results);
    for (const float value : {-0.1f,
                              1.1f,
                              std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity()}) {
        f.probability = value;
        EXPECT_FALSE(f.parser.parse(f.input, f.results));
    }
    f.probability = 0.5f;
    for (const double threshold : {-0.1, 1.1, std::numeric_limits<double>::quiet_NaN()}) {
        f.attributes.set("threshold", threshold);
        EXPECT_FALSE(f.parser.parse(f.input, f.results));
    }
    f.attributes.clear();
    opk::TensorView wrongShape{
        &f.probability, sizeof(float), opk::Shape(1), opk::Dtype::Float32, 1, 0};
    f.input.tensors[0] = &wrongShape;
    EXPECT_FALSE(f.parser.parse(f.input, f.results));
    f.input.tensors[0] = nullptr;
    EXPECT_FALSE(f.parser.parse(f.input, f.results));
    EXPECT_EQ(open_perception_kit::serialize(f.results), before);
}

TEST(SmartTurnParser, RejectsInvalidAudioRangesWithoutPublishing) {
    Fixture f;
    const auto before = open_perception_kit::serialize(f.results);
    auto &audio = f.input.inferenceInfo.audio;
    audio.windowEndSample = audio.windowStartSample;
    EXPECT_FALSE(f.parser.parse(f.input, f.results));
    audio.windowEndSample = 0;
    EXPECT_FALSE(f.parser.parse(f.input, f.results));
    audio.windowEndSample = 128512;
    audio.hopSampleCount = 0;
    EXPECT_FALSE(f.parser.parse(f.input, f.results));
    audio.hopSampleCount = 128001;
    EXPECT_FALSE(f.parser.parse(f.input, f.results));
    audio.hopSampleCount = 512;
    audio.windowStartTimestampNs = 10;
    EXPECT_FALSE(f.parser.parse(f.input, f.results));
    audio.windowEndTimestampNs = 10;
    EXPECT_FALSE(f.parser.parse(f.input, f.results));
    audio.windowEndTimestampNs = 9;
    EXPECT_FALSE(f.parser.parse(f.input, f.results));
    audio.windowStartTimestampNs.reset();
    EXPECT_FALSE(f.parser.parse(f.input, f.results));
    EXPECT_EQ(open_perception_kit::serialize(f.results), before);
}

TEST(TurnDetections, EmptyPayloadAndOptionalLayerRoundTrip) {
    open_perception_kit::FrameResults results;
    results.add(Payload{});
    const auto bytes = open_perception_kit::serialize(results);
    open_perception_kit::FrameResults decoded{std::span<const uint8_t>(bytes)};
    const auto payload = decoded.get<Payload>();
    ASSERT_TRUE(payload.has_value());
    EXPECT_TRUE(payload->value().detections.empty());
    EXPECT_FALSE(payload->value().layer);
}

} // namespace
