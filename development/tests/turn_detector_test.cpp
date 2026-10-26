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

#include "TurnDetectorOp.h"
#include <array>
#include <gtest/gtest.h>

namespace {
using opk::op::OpSignal;
using Payload = open_perception_kit::metadata::VoiceActivityDetectionsT;

class NoOp : public opk::op::Op {
  public:
    opk::Result<void> configure(const opk::AttributeMap &) override {
        return {};
    }
    opk::Result<void> bind(size_t, const std::vector<Op *> &) override {
        return {};
    }
    opk::Result<OpSignal> process(opk::op::OpChainContext &) override {
        return OpSignal::Continue;
    }
};
class Vad : public NoOp, public opk::op::OpInterfacePostprocessor {
  public:
    std::vector<std::string_view> getProvidedContentTypes() const override {
        return {"voiceActivity"};
    }
};
class Inference : public NoOp, public opk::op::OpInterfaceInference {
  public:
    opk::Model model;
    mutable std::array<float, 256> data{};
    Inference() {
        opk::ModelInput input;
        input.dataKind = opk::DataKind::AudioPcm;
        input.valueType = opk::Dtype::Float32;
        input.shape = opk::Shape(1, 256);
        model.inputs.push_back(input);
        model.name = "smart-turn";
        model.contentType = "turnDetection";
    }
    const opk::Model &getModel() const override {
        return model;
    }
    uint8_t *getTensorDataAddress(size_t) const override {
        return reinterpret_cast<uint8_t *>(data.data());
    }
};

class TurnDetector : public testing::Test {
  protected:
    Vad vad;
    Inference inference;
    opk::stdop::TurnDetectorOp gate;
    opk::PcmRingBuffer source{37}; // Wrap inside hops to exercise both view spans.
    open_perception_kit::FrameResults results;
    opk::op::OpChainContext context;
    void SetUp() override {
        opk::AttributeMap attrs;
        attrs.set("hopSampleCount", int64_t{16});
        attrs.set("silenceMs", int64_t{2});
        attrs.set("retryMs", int64_t{2});
        ASSERT_TRUE(gate.configure(attrs));
        ASSERT_TRUE(gate.bind(1, {&vad, &gate, &inference}));
    }
    void addVad(uint64_t begin, bool speech, std::string_view producer = "vad") {
        Payload payload;
        payload.layer = open_perception_kit::makeLayerInfo(
            {.model = "silero-vad", .inferElementId = producer, .contentType = "voiceActivity"});
        auto detection = std::make_unique<open_perception_kit::metadata::VoiceActivityDetectionT>();
        detection->speech_active = speech;
        detection->window_start_sample = begin;
        detection->window_end_sample = begin + 16;
        payload.detections.push_back(std::move(detection));
        results.add(std::move(payload));
    }
    opk::Result<OpSignal> step(bool speech, bool reset = false, bool publish = true) {
        std::array<float, 16> samples;
        samples.fill(0.1f);
        EXPECT_TRUE(source.append(samples));
        const auto window = source.latest(16);
        if (!window.has_value()) {
            ADD_FAILURE() << "Expected a complete 16-sample PCM window";
            return tl::unexpected(
                OPK_ERROR(opk::ErrorFlag::InvalidData, "Test PCM window is missing"));
        }
        context.pcmViews.insert_or_assign("pipelineAudioData", *window);
        context.frameResults = &results;
        context.inferenceInfo.inferElementId = "vad";
        auto &audio = context.inferenceInfo.audio;
        audio.hopSampleCount = 16;
        audio.windowStartSample = source.endPosition() - 16;
        audio.windowEndSample = source.endPosition();
        audio.windowStartTimestampNs = audio.windowStartSample * 62500;
        audio.windowEndTimestampNs = audio.windowEndSample * 62500;
        audio.discontinuity = reset;
        if (publish)
            addVad(audio.windowStartSample, speech);
        return gate.process(context);
    }
    void expectStep(bool speech, OpSignal signal, bool reset = false) {
        const auto result = step(speech, reset);
        ASSERT_TRUE(result) << result.error().toString();
        EXPECT_EQ(*result, signal);
    }
};

TEST_F(TurnDetector, WaitsForSpeechThenSilenceAndBoundsRetries) {
    for (int i = 0; i < 20; ++i)
        expectStep(false, OpSignal::AbortChain);
    expectStep(true, OpSignal::AbortChain);
    expectStep(true, OpSignal::AbortChain);
    expectStep(false, OpSignal::AbortChain);
    expectStep(false, OpSignal::Continue); // 2 ms after the last voiced hop.
    expectStep(false, OpSignal::AbortChain);
    expectStep(false, OpSignal::Continue); // Retry only after another 2 ms.
    expectStep(false, OpSignal::AbortChain);
    expectStep(false, OpSignal::Continue);
    for (int i = 0; i < 5; ++i)
        expectStep(false, OpSignal::AbortChain); // Three attempts, then quiet.
    expectStep(true, OpSignal::AbortChain);
    expectStep(false, OpSignal::AbortChain);
    expectStep(false, OpSignal::Continue); // New speech rearms the gate.
}

TEST_F(TurnDetector, PadsStartupAndPreservesRealAudioBoundsAndVadResults) {
    expectStep(true, OpSignal::AbortChain);
    expectStep(false, OpSignal::AbortChain);
    expectStep(false, OpSignal::Continue);
    EXPECT_EQ(gate.getRequiredAudioSamples(), 16u);
    for (size_t i = 0; i < inference.data.size(); ++i)
        EXPECT_FLOAT_EQ(inference.data[i], i < 208 ? 0.0f : 0.1f);
    EXPECT_EQ(context.inferenceInfo.modelName, "smart-turn");
    EXPECT_EQ(context.inferenceInfo.audio.windowStartSample, 0u);
    EXPECT_EQ(context.inferenceInfo.audio.windowEndSample, 48u);
    EXPECT_EQ(context.inferenceInfo.audio.windowStartTimestampNs, 0u);
    EXPECT_EQ(context.inferenceInfo.audio.windowEndTimestampNs, 3000000u);
    size_t count = 0;
    results.for_each<Payload>([&](const auto &) { ++count; });
    EXPECT_EQ(count, 3u);
}

TEST_F(TurnDetector, KeepsOnlyLatestModelWindowAfterRingWraps) {
    for (int i = 0; i < 20; ++i)
        expectStep(true, OpSignal::AbortChain);
    expectStep(false, OpSignal::AbortChain);
    expectStep(false, OpSignal::Continue);
    for (const auto value : inference.data)
        EXPECT_FLOAT_EQ(value, 0.1f);
    EXPECT_EQ(context.inferenceInfo.audio.windowStartSample, 96u);
    EXPECT_EQ(context.inferenceInfo.audio.windowEndSample, 352u);
    EXPECT_EQ(context.inferenceInfo.audio.windowStartTimestampNs, 6000000u);
}

TEST_F(TurnDetector, ResetsOnDiscontinuitiesAndGaps) {
    expectStep(true, OpSignal::AbortChain);
    expectStep(false, OpSignal::AbortChain, true);
    for (int i = 0; i < 4; ++i)
        expectStep(false, OpSignal::AbortChain);
    expectStep(true, OpSignal::AbortChain);
    source.clear(1000);
    expectStep(false, OpSignal::AbortChain);
    expectStep(false, OpSignal::AbortChain);
    expectStep(true, OpSignal::AbortChain);
    expectStep(false, OpSignal::AbortChain);
    expectStep(false, OpSignal::Continue);
    EXPECT_EQ(context.inferenceInfo.audio.windowStartSample, 1000u);
    EXPECT_FLOAT_EQ(inference.data.front(), 0.0f);
}

TEST_F(TurnDetector, ReportsResetOnFirstAssessmentAfterSkippedHops) {
    expectStep(true, OpSignal::AbortChain, true);
    expectStep(false, OpSignal::AbortChain);
    expectStep(false, OpSignal::Continue);
    EXPECT_TRUE(context.inferenceInfo.audio.discontinuity);
    expectStep(false, OpSignal::AbortChain);
    expectStep(false, OpSignal::Continue);
    EXPECT_FALSE(context.inferenceInfo.audio.discontinuity);
    expectStep(true, OpSignal::AbortChain); // New speech alone is not a discontinuity.
    expectStep(false, OpSignal::AbortChain);
    expectStep(false, OpSignal::Continue);
    EXPECT_FALSE(context.inferenceInfo.audio.discontinuity);
    expectStep(false, OpSignal::AbortChain, true);
    expectStep(true, OpSignal::AbortChain);
    expectStep(false, OpSignal::AbortChain);
    expectStep(false, OpSignal::Continue);
    EXPECT_TRUE(context.inferenceInfo.audio.discontinuity);
    source.clear(1000); // A gap also resets the model history.
    expectStep(true, OpSignal::AbortChain);
    expectStep(false, OpSignal::AbortChain);
    expectStep(false, OpSignal::Continue);
    EXPECT_TRUE(context.inferenceInfo.audio.discontinuity);
}

TEST_F(TurnDetector, RejectsMissingOrAmbiguousCurrentVadRatherThanUsingStaleResults) {
    expectStep(true, OpSignal::AbortChain);
    addVad(16, true, "another-infer-element");
    EXPECT_FALSE(step(false, false, false)); // Neither stale nor foreign VAD is usable.
    addVad(32, false);
    EXPECT_FALSE(step(false)); // Two exact matches are ambiguous.
}

TEST_F(TurnDetector, RejectsWrongHopAndMissingContext) {
    expectStep(true, OpSignal::AbortChain);
    context.inferenceInfo.audio.hopSampleCount = 8;
    EXPECT_FALSE(gate.process(context));
    context.inferenceInfo.audio.hopSampleCount = 16;
    context.frameResults = nullptr;
    EXPECT_FALSE(gate.process(context));
}

TEST_F(TurnDetector, ValidatesConfigurationAndBinding) {
    EXPECT_FALSE(gate.bind(0, {}));
    EXPECT_FALSE(gate.bind(1, {&vad, &gate}));
    EXPECT_FALSE(gate.bind(1, {&inference, &gate, &inference}));
    gate.loopId = 1;
    EXPECT_FALSE(gate.bind(1, {&vad, &gate, &inference}));
    gate.loopId = 0;
    inference.model.inputs[0].dataKind = opk::DataKind::AudioLogMel;
    EXPECT_FALSE(gate.bind(1, {&vad, &gate, &inference}));
    inference.model.inputs[0].dataKind = opk::DataKind::AudioPcm;
    inference.model.inputs[0].shape = opk::Shape(2, 128);
    EXPECT_FALSE(gate.bind(1, {&vad, &gate, &inference}));
    for (const auto *name : {"hopSampleCount", "retryMs", "maxAttempts"}) {
        opk::AttributeMap attrs;
        attrs.set(name, int64_t{0});
        EXPECT_FALSE(gate.configure(attrs));
    }
}
} // namespace
