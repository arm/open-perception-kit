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

#include "postproc/SileroVadParser.h"
#include <array>
#include <gtest/gtest.h>
#include <limits>

namespace {
using Detection = open_perception_kit::metadata::VoiceActivityDetectionT;
using Payload = open_perception_kit::metadata::VoiceActivityDetectionsT;
struct Fixture {
    opk::stdop::postproc::SileroVadParser parser;
    opk::AttributeMap attributes;
    std::array<float, 1> probabilities{0.9f};
    opk::TensorView tensor{
        probabilities.data(), sizeof(probabilities), opk::Shape(1, 1), opk::Dtype::Float32, 1, 0};
    opk::TensorParser::Input input{attributes};
    open_perception_kit::FrameResults results;
    Fixture() {
        input.tensors[0] = &tensor;
        input.inferenceInfo.audio.hopSampleCount = 320;
        input.inferenceInfo.audio.windowEndSample = 320;
        input.inferenceInfo.audio.windowStartTimestampNs = 0;
        input.inferenceInfo.audio.windowEndTimestampNs = 20000000;
        input.inferenceInfo.inferElementId = "vad0";
        input.inferenceInfo.modelName = "silero-vad";
    }
    bool step(float p, bool reset = false) {
        probabilities[0] = p;
        input.inferenceInfo.audio.discontinuity = reset;
        auto result = parser.parse(input, results);
        EXPECT_TRUE(result) << (result ? "" : result.error().toString());
        bool active = false;
        results.for_each<Payload>(
            [&](const auto &payload) { active = payload.detections[0]->speech_active; });
        input.inferenceInfo.audio.windowStartSample += input.inferenceInfo.audio.hopSampleCount;
        input.inferenceInfo.audio.windowEndSample += input.inferenceInfo.audio.hopSampleCount;
        *input.inferenceInfo.audio.windowStartTimestampNs +=
            input.inferenceInfo.audio.hopSampleCount * 62500;
        *input.inferenceInfo.audio.windowEndTimestampNs +=
            input.inferenceInfo.audio.hopSampleCount * 62500;
        return active;
    }
};

TEST(SileroVadParser, UsesProbabilitiesAndCountsConsecutiveHopSamples) {
    Fixture f;
    EXPECT_FALSE(f.step(0.9f));
    EXPECT_FALSE(f.step(0.9f));
    EXPECT_TRUE(f.step(0.9f)); // 50 ms = 800 samples; three 320-sample hops.
    EXPECT_TRUE(f.step(0.5f)); // Hysteresis band preserves active state.
    EXPECT_TRUE(f.step(0.1f));
    EXPECT_TRUE(f.step(0.1f));
    EXPECT_TRUE(f.step(0.5f)); // Interrupt release run.
    EXPECT_TRUE(f.step(0.1f));
    EXPECT_TRUE(f.step(0.1f));
    EXPECT_FALSE(f.step(0.1f));
    size_t count = 0;
    f.results.for_each<Payload>([&](const auto &payload) {
        if (count++ == 0) {
            EXPECT_FLOAT_EQ(payload.detections[0]->speech_probability, 0.9f);
            EXPECT_EQ(payload.layer->content_type, "voiceActivity");
        }
    });
    EXPECT_EQ(count, 10u); // Burst outputs are all retained.
}

TEST(SileroVadParser, ActivatesAfterTwo512SampleHops) {
    Fixture f;
    f.input.inferenceInfo.audio.hopSampleCount = 512;
    f.input.inferenceInfo.audio.windowEndSample = 512;
    EXPECT_FALSE(f.step(0.9f));
    EXPECT_TRUE(f.step(0.9f));
    EXPECT_TRUE(f.step(0.1f));
    EXPECT_FALSE(f.step(0.1f));
}

TEST(SileroVadParser, ResetsAndKeepsInstancesIndependent) {
    Fixture a, b;
    EXPECT_FALSE(a.step(0.9f));
    EXPECT_FALSE(a.step(0.9f));
    EXPECT_FALSE(a.step(0.9f, true));
    EXPECT_FALSE(a.step(0.9f));
    EXPECT_TRUE(a.step(0.9f));
    EXPECT_FALSE(b.step(0.9f));
    EXPECT_FALSE(a.step(0.1f, true));
}

TEST(SileroVadParser, HonorsHopInsteadOfWindowSizeAndAllowsZeroDelay) {
    Fixture f;
    f.input.inferenceInfo.audio.hopSampleCount = 160;
    for (int i = 0; i < 4; ++i)
        EXPECT_FALSE(f.step(0.9f));
    EXPECT_TRUE(f.step(0.9f));
    f.attributes.set("attackMs", int64_t{0});
    f.attributes.set("releaseMs", int64_t{0});
    EXPECT_FALSE(f.step(0.1f));
    EXPECT_TRUE(f.step(0.9f));
}

TEST(SileroVadParser, RejectsMalformedInputsWithoutPublishingResults) {
    Fixture f;
    f.probabilities[0] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(f.parser.parse(f.input, f.results));
    f.probabilities[0] = 1.1f;
    EXPECT_FALSE(f.parser.parse(f.input, f.results));
    f.probabilities[0] = 0.9f;
    f.attributes.set("windowMs", int64_t{50});
    EXPECT_FALSE(f.parser.parse(f.input, f.results));
    f.attributes.clear();
    f.attributes.set("thresholdOff", 0.8);
    EXPECT_FALSE(f.parser.parse(f.input, f.results));
    f.attributes.clear();
    f.attributes.set("attackMs", int64_t{-1});
    EXPECT_FALSE(f.parser.parse(f.input, f.results));
    f.attributes.clear();
    f.input.inferenceInfo.audio.hopSampleCount = 0;
    EXPECT_FALSE(f.parser.parse(f.input, f.results));
    f.input.inferenceInfo.audio.hopSampleCount = 320;
    f.input.tensors[0] = nullptr;
    EXPECT_FALSE(f.parser.parse(f.input, f.results));
    size_t count = 0;
    f.results.for_each<Payload>([&](const auto &) { ++count; });
    EXPECT_EQ(count, 0u);
}

TEST(SileroVadParser, RoundTripsTimestampsIncludingZeroAndAbsentValues) {
    Fixture f;
    EXPECT_FALSE(f.step(0.9f, true));
    f.input.inferenceInfo.audio.windowStartTimestampNs.reset();
    f.input.inferenceInfo.audio.windowEndTimestampNs.reset();
    ASSERT_TRUE(f.parser.parse(f.input, f.results));
    const auto bytes = open_perception_kit::serialize(f.results);
    open_perception_kit::FrameResults decoded{std::span<const uint8_t>(bytes)};
    size_t count = 0;
    decoded.for_each<Payload>([&](const auto &payload) {
        const auto &d = *payload.detections[0];
        if (count++ == 0) {
            ASSERT_TRUE(d.window_start_ts_ns);
            EXPECT_EQ(*d.window_start_ts_ns, 0u);
            EXPECT_EQ(*d.window_end_ts_ns, 20000000u);
            EXPECT_TRUE(d.discontinuity);
            EXPECT_EQ(d.window_start_sample, 0u);
            EXPECT_EQ(d.window_end_sample, 320u);
        } else {
            EXPECT_FALSE(d.window_start_ts_ns);
            EXPECT_FALSE(d.window_end_ts_ns);
        }
    });
    EXPECT_EQ(count, 2u);
}

TEST(VoiceActivityDetections, EmptyPayloadAndOptionalLayerRoundTrip) {
    open_perception_kit::FrameResults results;
    results.add(Payload{});
    const auto bytes = open_perception_kit::serialize(results);
    open_perception_kit::FrameResults decoded{std::span<const uint8_t>(bytes)};
    const auto payload = decoded.get<Payload>();
    ASSERT_TRUE(payload);
    EXPECT_TRUE(payload.value().value().detections.empty());
    EXPECT_FALSE(payload.value().value().layer);
}
} // namespace
