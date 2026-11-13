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

#include "GenericAudioPreprocessOp.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>
#include <string_view>

namespace {
class Inference : public opk::op::Op, public opk::op::OpInterfaceInference {
  public:
    opk::Model model;
    mutable std::vector<float> data = std::vector<float>(4);
    bool missing = false;
    uint8_t *destinationOverride = nullptr;
    mutable size_t lastTensorIndex = opk::InvalidTensorIndex;
    Inference() {
        opk::ModelInput input;
        input.dataKind = opk::DataKind::AudioPcm;
        input.shape = opk::Shape(1, 4);
        model.inputs.push_back(input);
        model.name = "audio";
        model.contentType = "voiceActivity";
    }
    opk::Result<void> configure(const opk::AttributeMap &) override {
        return {};
    }
    opk::Result<void> bind(size_t, const std::vector<opk::op::Op *> &) override {
        return {};
    }
    opk::Result<opk::op::OpSignal> process(opk::op::OpChainContext &) override {
        return opk::op::OpSignal::Continue;
    }
    const opk::Model &getModel() const override {
        return model;
    }
    uint8_t *getTensorDataAddress(const size_t index) const override {
        lastTensorIndex = index;
        return missing               ? nullptr
               : destinationOverride ? destinationOverride
                                     : reinterpret_cast<uint8_t *>(data.data());
    }
};

TEST(GenericAudioPreprocess, CopiesWrappedWindowAndPreservesTiming) {
    Inference inference;
    opk::stdop::GenericAudioPreprocessOp pre;
    ASSERT_TRUE(pre.configure({}));
    ASSERT_TRUE(pre.bind(0, {&pre, &inference}));
    opk::PcmRingBuffer ring(5);
    ASSERT_TRUE(ring.append(std::array<float, 4>{0, 1, 2, 3}));
    ASSERT_TRUE(ring.append(std::array<float, 3>{4, 5, 6}));
    opk::op::OpChainContext ctx;
    const auto window1 = ring.window(3, 4);
    if (!window1.has_value())
        FAIL() << "Expected window1 to contain a value";
    ctx.pcmViews.emplace("pipelineAudioData", *window1);
    ctx.inferenceInfo.audio.hopSampleCount = 2;
    ctx.inferenceInfo.audio.windowStartTimestampNs = 0;
    ctx.inferenceInfo.audio.discontinuity = true;
    ASSERT_TRUE(pre.process(ctx));
    EXPECT_EQ(inference.data, (std::vector<float>{3, 4, 5, 6}));
    EXPECT_EQ(ctx.inferenceInfo.modelName, "audio");
    EXPECT_EQ(ctx.inferenceInfo.audio.hopSampleCount, 2);
    EXPECT_EQ(ctx.inferenceInfo.audio.windowStartTimestampNs, 0);
    EXPECT_TRUE(ctx.inferenceInfo.audio.discontinuity);
}

TEST(GenericAudioPreprocess, RejectsMissingWrongSizeAndNullDestinationWithoutWriting) {
    Inference inference;
    opk::stdop::GenericAudioPreprocessOp pre;
    opk::op::OpChainContext ctx;
    EXPECT_FALSE(pre.process(ctx));
    ASSERT_TRUE(pre.configure({}));
    ASSERT_TRUE(pre.bind(0, {&pre, &inference}));
    EXPECT_FALSE(pre.process(ctx));
    opk::PcmRingBuffer ring(5);
    ASSERT_TRUE(ring.append(std::array<float, 5>{1, 2, 3, 4, 5}));
    ctx.pcmViews.emplace("pipelineAudioData", ring.view());
    EXPECT_FALSE(pre.process(ctx));
    const auto window2 = ring.latest(3);
    if (!window2.has_value())
        FAIL() << "Expected window2 to contain a value";
    ctx.pcmViews.insert_or_assign("pipelineAudioData", *window2);
    EXPECT_FALSE(pre.process(ctx));
    const auto window3 = ring.latest(4);
    if (!window3.has_value())
        FAIL() << "Expected window3 to contain a value";
    ctx.pcmViews.insert_or_assign("pipelineAudioData", *window3);
    inference.missing = true;
    EXPECT_FALSE(pre.process(ctx));
    EXPECT_EQ(inference.data, std::vector<float>(4));
}

TEST(GenericAudioPreprocess, ValidatesBindingAndTensorContract) {
    Inference inference;
    opk::stdop::GenericAudioPreprocessOp pre;
    EXPECT_FALSE(pre.bind(0, {}));
    EXPECT_FALSE(pre.bind(0, {&pre}));
    EXPECT_FALSE(pre.bind(0, {&pre, &pre}));
    auto dynamicShape = opk::Shape(1, 4);
    dynamicShape.dims[1] = -1;
    for (const auto shape : {opk::Shape(), opk::Shape(2, 4), dynamicShape}) {
        inference.model.inputs[0].shape = shape;
        EXPECT_FALSE(pre.bind(0, {&pre, &inference}));
    }
    inference.model.inputs[0].shape = opk::Shape(4);
    inference.model.inputs[0].valueType = opk::Dtype::Uint8;
    EXPECT_FALSE(pre.bind(0, {&pre, &inference}));
    inference.model.inputs[0].valueType = opk::Dtype::Float32;
    inference.model.inputs[0].dataKind = opk::DataKind::AudioLogMel;
    inference.model.inputs[0].shape = opk::Shape(1, 1, 24);
    EXPECT_FALSE(pre.bind(0, {&pre, &inference}));
    inference.model.inputs[0].dataKind = opk::DataKind::AudioPcm;
    inference.model.inputs[0].shape = opk::Shape(4);
    ASSERT_TRUE(pre.bind(0, {&pre, &inference}));
}

TEST(GenericAudioPreprocess, ExplicitSelectionAndCustomSource) {
    Inference inference;
    inference.model.inputs.push_back(inference.model.inputs[0]);
    opk::stdop::GenericAudioPreprocessOp pre;
    EXPECT_FALSE(pre.bind(0, {&pre, &inference}));
    opk::AttributeMap attrs;
    attrs.raw()["inputAudioTensorIndex"] = 1;
    attrs.raw()["inputAudioSourceName"] = "custom";
    ASSERT_TRUE(pre.configure(attrs));
    ASSERT_TRUE(pre.bind(0, {&pre, &inference}));
    opk::PcmRingBuffer ring(4);
    ASSERT_TRUE(ring.append(std::array<float, 4>{1, 2, 3, 4}));
    opk::op::OpChainContext ctx;
    ctx.pcmViews.emplace("custom", ring.view());
    ASSERT_TRUE(pre.process(ctx));
    EXPECT_EQ(inference.data, (std::vector<float>{1, 2, 3, 4}));
    attrs.raw()["inputAudioTensorIndex"] = 99;
    ASSERT_TRUE(pre.configure(attrs));
    EXPECT_FALSE(pre.bind(0, {&pre, &inference}));
    attrs.raw()["inputAudioTensorIndex"] = -2;
    EXPECT_FALSE(pre.configure(attrs));
}
TEST(GenericAudioPreprocess, PrependsContextAndResetsAtDiscontinuity) {
    Inference inference;
    opk::stdop::GenericAudioPreprocessOp pre;
    opk::AttributeMap attrs;
    attrs.set("contextSampleCount", int64_t{1});
    ASSERT_TRUE(pre.configure(attrs));
    ASSERT_TRUE(pre.bind(0, {&pre, &inference}));
    EXPECT_EQ(pre.getRequiredAudioSamples(), 3u);
    opk::PcmRingBuffer ring(4);
    ASSERT_TRUE(ring.append(std::array<float, 3>{1, 2, 3}));
    opk::op::OpChainContext ctx;
    ctx.inferenceInfo.audio.hopSampleCount = 3;
    ctx.inferenceInfo.audio.discontinuity = true;
    const auto window4 = ring.latest(3);
    if (!window4.has_value())
        FAIL() << "Expected window4 to contain a value";
    ctx.pcmViews.emplace("pipelineAudioData", *window4);
    ASSERT_TRUE(pre.process(ctx));
    EXPECT_EQ(inference.data, (std::vector<float>{0, 1, 2, 3}));
    ASSERT_TRUE(ring.append(std::array<float, 3>{4, 5, 6}));
    const auto window5 = ring.latest(3);
    if (!window5.has_value())
        FAIL() << "Expected window5 to contain a value";
    ctx.pcmViews.insert_or_assign("pipelineAudioData", *window5);
    ctx.inferenceInfo.audio.discontinuity = false;
    ASSERT_TRUE(pre.process(ctx));
    EXPECT_EQ(inference.data, (std::vector<float>{3, 4, 5, 6}));
    EXPECT_FALSE(pre.process(ctx)); // Duplicate window must not advance model context.
    ctx.inferenceInfo.audio.discontinuity = true;
    ASSERT_TRUE(pre.process(ctx));
    EXPECT_EQ(inference.data, (std::vector<float>{0, 4, 5, 6}));
    ctx.inferenceInfo.audio.hopSampleCount = 2;
    EXPECT_FALSE(pre.process(ctx));
    attrs.set("contextSampleCount", int64_t{4});
    ASSERT_TRUE(pre.configure(attrs));
    EXPECT_FALSE(pre.bind(0, {&pre, &inference}));
    attrs.set("contextSampleCount", int64_t{-1});
    EXPECT_FALSE(pre.configure(attrs));
}

opk::AttributeMap logMelAttributes(const int64_t window = 384,
                                   const int64_t hop = 128,
                                   const char *normalization = "rolling") {
    auto logMel = std::make_shared<opk::AttributeMap>();
    logMel->set("windowSampleCount", window);
    logMel->set("frameHopSamples", hop);
    logMel->set("fftSize", int64_t{384});
    logMel->set("windowLengthSamples", int64_t{300});
    logMel->set("minFrequencyHz", 50.0);
    logMel->set("maxFrequencyHz", 7800.0);
    logMel->set("logOffset", 1e-7);
    logMel->set("layout", "time-major");
    auto norm = std::make_shared<opk::AttributeMap>();
    norm->set("kind", normalization);
    if (std::string_view(normalization) != "none")
        norm->set("epsilon", 1e-6);
    if (std::string_view(normalization) == "rolling")
        norm->set("historyFrames", int64_t{7});
    logMel->setObject("normalization", norm);
    opk::AttributeMap attributes;
    attributes.setObject("logMel", logMel);
    return attributes;
}

TEST(GenericAudioPreprocess, ValidatesLogMelConfigurationAndTensorAgreement) {
    Inference inference;
    opk::stdop::GenericAudioPreprocessOp pre;
    auto attributes = logMelAttributes();
    ASSERT_TRUE(pre.configure(attributes));
    EXPECT_FALSE(pre.bind(0, {&pre, &inference})); // PCM cannot consume log-mel attributes.
    inference.model.inputs[0].dataKind = opk::DataKind::AudioLogMel;
    for (const auto shape :
         {opk::Shape(1, 2, 24), opk::Shape(2, 1, 24), opk::Shape(24), opk::Shape(1, 1, 513)}) {
        inference.model.inputs[0].shape = shape;
        EXPECT_FALSE(pre.bind(0, {&pre, &inference}));
    }
    inference.model.inputs[0].shape = opk::Shape(1, 1, 24);
    ASSERT_TRUE(pre.bind(0, {&pre, &inference}));
    EXPECT_EQ(pre.getRequiredAudioSamples(), 384u);
    attributes.set("contextSampleCount", int64_t{0});
    EXPECT_FALSE(pre.configure(attributes));
    for (const auto &invalid : {logMelAttributes(383),
                                logMelAttributes(960001),
                                logMelAttributes(384, 0),
                                logMelAttributes(384, 385),
                                logMelAttributes(384, 128, "other"),
                                logMelAttributes(512)})
        EXPECT_FALSE(pre.configure(invalid));
    attributes = logMelAttributes();
    attributes.getObject("logMel")->set("unexpected", true);
    EXPECT_FALSE(pre.configure(attributes));
    attributes.set("logMel", int64_t{1});
    EXPECT_FALSE(pre.configure(attributes));
    attributes = {};
    attributes.set("inputAudioTensorIndex", 0.0);
    EXPECT_FALSE(pre.configure(attributes));
    attributes = {};
    attributes.set("inputAudioSourceName", true);
    EXPECT_FALSE(pre.configure(attributes));
}

TEST(GenericAudioPreprocess, BuildsWindowLogMelAndPreservesSchedulerMetadata) {
    Inference inference;
    inference.model.inputs[0].dataKind = opk::DataKind::AudioLogMel;
    inference.model.inputs[0].shape = opk::Shape(1, 13, 24);
    inference.data.resize(size_t{13} * 24);
    opk::stdop::GenericAudioPreprocessOp pre;
    ASSERT_TRUE(pre.configure(logMelAttributes(2000, 128, "window")));
    ASSERT_TRUE(pre.bind(0, {&pre, &inference}));
    EXPECT_EQ(pre.getRequiredAudioSamples(), 2000u);
    opk::PcmRingBuffer ring(2000);
    std::vector<float> samples(2000);
    samples[192] = 1;
    ASSERT_TRUE(ring.append(samples));
    opk::op::OpChainContext context;
    context.pcmViews.emplace("pipelineAudioData", ring.view());
    context.inferenceInfo.audio.hopSampleCount = 128;
    context.inferenceInfo.audio.windowStartSample = 0;
    context.inferenceInfo.audio.windowEndSample = 2000;
    context.inferenceInfo.audio.windowStartTimestampNs = 123;
    context.inferenceInfo.audio.windowEndTimestampNs = 125000123;
    context.inferenceInfo.audio.discontinuity = true;
    ASSERT_TRUE(pre.process(context));
    EXPECT_TRUE(std::all_of(inference.data.begin(), inference.data.end(), [](const float value) {
        return std::isfinite(value);
    }));
    EXPECT_GT(inference.data[0], 1);
    EXPECT_EQ(context.inferenceInfo.audio.windowStartSample, 0u);
    EXPECT_EQ(context.inferenceInfo.audio.windowEndSample, 2000u);
    EXPECT_EQ(context.inferenceInfo.audio.windowStartTimestampNs, 123u);
    EXPECT_EQ(context.inferenceInfo.audio.windowEndTimestampNs, 125000123u);
    EXPECT_EQ(context.inferenceInfo.audio.hopSampleCount, 128u);
    EXPECT_TRUE(context.inferenceInfo.audio.discontinuity);
}

TEST(GenericAudioPreprocess, RollingCadenceAndDiscontinuityControlHistory) {
    Inference inference;
    inference.model.inputs[0].dataKind = opk::DataKind::AudioLogMel;
    inference.model.inputs[0].shape = opk::Shape(1, 1, 24);
    inference.data.resize(24);
    opk::stdop::GenericAudioPreprocessOp pre;
    ASSERT_TRUE(pre.configure(logMelAttributes()));
    ASSERT_TRUE(pre.bind(0, {&pre, &inference}));
    opk::PcmRingBuffer ring(512);
    std::vector<float> samples(512);
    samples[400] = 1;
    ASSERT_TRUE(ring.append(samples));
    opk::op::OpChainContext context;
    context.inferenceInfo.audio.hopSampleCount = 128;
    const auto first = ring.window(0, 384);
    const auto second = ring.window(128, 384);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    context.pcmViews.emplace("pipelineAudioData", *first);
    ASSERT_TRUE(pre.process(context));
    EXPECT_EQ(inference.data, std::vector<float>(24));
    EXPECT_FALSE(pre.process(context));
    context.pcmViews.insert_or_assign("pipelineAudioData", *second);
    context.inferenceInfo.audio.hopSampleCount = 384;
    EXPECT_FALSE(pre.process(context));
    context.inferenceInfo.audio.hopSampleCount = 128;
    ASSERT_TRUE(pre.process(context));
    const auto withHistory = inference.data;
    context.inferenceInfo.audio.discontinuity = true;
    ASSERT_TRUE(pre.process(context));
    EXPECT_NE(inference.data, withHistory);
    const auto afterReset = inference.data;
    ASSERT_TRUE(pre.configure(logMelAttributes()));
    ASSERT_TRUE(pre.bind(0, {&pre, &inference}));
    context.inferenceInfo.audio.discontinuity = false;
    ASSERT_TRUE(pre.process(context));
    EXPECT_EQ(inference.data, afterReset);
}

TEST(GenericAudioPreprocess, RejectsNonfinitePcmWithoutWriting) {
    Inference inference;
    opk::stdop::GenericAudioPreprocessOp pre;
    ASSERT_TRUE(pre.configure({}));
    ASSERT_TRUE(pre.bind(0, {&pre, &inference}));
    opk::PcmRingBuffer ring(4);
    ASSERT_TRUE(
        ring.append(std::array<float, 4>{0, 0, std::numeric_limits<float>::quiet_NaN(), 0}));
    opk::op::OpChainContext context;
    context.pcmViews.emplace("pipelineAudioData", ring.view());
    EXPECT_FALSE(pre.process(context));
    EXPECT_EQ(inference.data, std::vector<float>(4));
}

TEST(GenericAudioPreprocess, CopiesPcmToUnalignedStorageWithAndWithoutContext) {
    for (const int64_t contextCount : {0, 1}) {
        Inference inference;
        std::array<uint8_t, 4 * sizeof(float) + 2> storage;
        storage.fill(0xAB);
        inference.destinationOverride = storage.data() + 1;
        opk::stdop::GenericAudioPreprocessOp pre;
        opk::AttributeMap attributes;
        attributes.set("contextSampleCount", contextCount);
        ASSERT_TRUE(pre.configure(attributes));
        ASSERT_TRUE(pre.bind(0, {&pre, &inference}));
        opk::PcmRingBuffer pcm(4);
        ASSERT_TRUE(pcm.append(std::array<float, 4>{1, 2, 3, 4}));
        const auto window = pcm.latest(pre.getRequiredAudioSamples());
        ASSERT_TRUE(window);
        opk::op::OpChainContext context;
        context.pcmViews.emplace("pipelineAudioData", *window);
        context.inferenceInfo.audio.hopSampleCount = contextCount ? 3 : 2;
        ASSERT_TRUE(pre.process(context));
        std::array<float, 4> output;
        std::memcpy(output.data(), storage.data() + 1, sizeof(output));
        EXPECT_EQ(
            output,
            (contextCount ? std::array<float, 4>{0, 2, 3, 4} : std::array<float, 4>{1, 2, 3, 4}));
        EXPECT_EQ(storage.front(), 0xAB);
        EXPECT_EQ(storage.back(), 0xAB);
    }
}

TEST(GenericAudioPreprocess, FailedPcmProcessingPreservesContextUntilSuccessfulReset) {
    Inference inference;
    opk::stdop::GenericAudioPreprocessOp pre;
    opk::AttributeMap attributes;
    attributes.set("contextSampleCount", int64_t{1});
    ASSERT_TRUE(pre.configure(attributes));
    ASSERT_TRUE(pre.bind(0, {&pre, &inference}));
    opk::PcmRingBuffer pcm(6);
    ASSERT_TRUE(pcm.append(std::array<float, 6>{1, 2, 3, 4, 5, 6}));
    const auto first = pcm.window(0, 3);
    const auto second = pcm.window(3, 3);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    opk::op::OpChainContext context;
    context.pcmViews.emplace("pipelineAudioData", *first);
    context.inferenceInfo.audio.hopSampleCount = 3;
    ASSERT_TRUE(pre.process(context));
    const auto initialOutput = inference.data;
    context.pcmViews.insert_or_assign("pipelineAudioData", *second);
    context.inferenceInfo.audio.discontinuity = true;
    inference.missing = true;
    EXPECT_FALSE(pre.process(context));
    inference.missing = false;
    context.inferenceInfo.audio.hopSampleCount = 2;
    EXPECT_FALSE(pre.process(context));
    context.inferenceInfo.audio.hopSampleCount = 3;
    opk::PcmRingBuffer invalid(3);
    ASSERT_TRUE(invalid.append(std::array<float, 3>{1, std::numeric_limits<float>::infinity(), 2}));
    context.pcmViews.insert_or_assign("pipelineAudioData", invalid.view());
    EXPECT_FALSE(pre.process(context));
    EXPECT_EQ(inference.data, initialOutput);
    context.pcmViews.insert_or_assign("pipelineAudioData", *second);
    context.inferenceInfo.audio.discontinuity = false;
    ASSERT_TRUE(pre.process(context));
    EXPECT_EQ(inference.data, (std::vector<float>{3, 4, 5, 6}));
    context.inferenceInfo.audio.discontinuity = true;
    ASSERT_TRUE(pre.process(context));
    EXPECT_EQ(inference.data, (std::vector<float>{0, 4, 5, 6}));
}

TEST(GenericAudioPreprocess, RebindReselectsInputAndResetsContextAfterSuccessOrFailure) {
    Inference inference;
    opk::stdop::GenericAudioPreprocessOp pre;
    opk::AttributeMap attributes;
    attributes.set("contextSampleCount", int64_t{1});
    ASSERT_TRUE(pre.configure(attributes));
    ASSERT_TRUE(pre.bind(0, {&pre, &inference}));
    opk::PcmRingBuffer pcm(3);
    ASSERT_TRUE(pcm.append(std::array<float, 3>{1, 2, 3}));
    opk::op::OpChainContext context;
    context.pcmViews.emplace("pipelineAudioData", pcm.view());
    context.inferenceInfo.audio.hopSampleCount = 3;
    ASSERT_TRUE(pre.process(context));
    EXPECT_EQ(inference.lastTensorIndex, 0u);
    const auto output = inference.data;
    EXPECT_FALSE(pre.bind(0, {}));
    EXPECT_EQ(pre.getRequiredAudioSamples(), 0u);
    EXPECT_FALSE(pre.process(context));
    EXPECT_EQ(inference.data, output);
    inference.model.inputs.push_back(inference.model.inputs[0]);
    inference.model.inputs[0].dataKind = opk::DataKind::RawTensorData;
    ASSERT_TRUE(pre.bind(0, {&pre, &inference}));
    ASSERT_TRUE(pre.process(context));
    EXPECT_EQ(inference.lastTensorIndex, 1u);
    EXPECT_EQ(inference.data, output);
    inference.model.inputs[1].shape = opk::Shape(2, 4);
    EXPECT_FALSE(pre.bind(0, {&pre, &inference}));
    EXPECT_EQ(pre.getRequiredAudioSamples(), 0u);
    EXPECT_FALSE(pre.process(context));
    inference.model.inputs[1].shape = opk::Shape(1, 4);
    ASSERT_TRUE(pre.bind(0, {&pre, &inference}));
    ASSERT_TRUE(pre.process(context));
    EXPECT_EQ(inference.data, output);
    ASSERT_TRUE(pre.bind(0, {&pre, &inference}));
    ASSERT_TRUE(pre.process(context));
    EXPECT_EQ(inference.data, output);
}

TEST(GenericAudioPreprocess, InvalidReconfigurationDisablesExecutionUntilValidConfiguration) {
    Inference inference;
    opk::stdop::GenericAudioPreprocessOp pre;
    opk::PcmRingBuffer pcm(4);
    ASSERT_TRUE(pcm.append(std::array<float, 4>{1, 2, 3, 4}));
    opk::op::OpChainContext context;
    context.pcmViews.emplace("pipelineAudioData", pcm.view());
    for (const auto *const name :
         {"inputAudioSourceName", "inputAudioTensorIndex", "contextSampleCount", "logMel"}) {
        ASSERT_TRUE(pre.configure({}));
        ASSERT_TRUE(pre.bind(0, {&pre, &inference}));
        ASSERT_TRUE(pre.process(context));
        const auto output = inference.data;
        opk::AttributeMap invalid;
        invalid.set(name, true);
        EXPECT_FALSE(pre.configure(invalid));
        EXPECT_EQ(pre.getRequiredAudioSamples(), 0u);
        EXPECT_FALSE(pre.process(context));
        EXPECT_FALSE(pre.bind(0, {&pre, &inference}));
        EXPECT_EQ(inference.data, output);
    }
    ASSERT_TRUE(pre.configure({}));
    ASSERT_TRUE(pre.bind(0, {&pre, &inference}));
    ASSERT_TRUE(pre.process(context));
    EXPECT_EQ(inference.data, (std::vector<float>{1, 2, 3, 4}));
}

TEST(GenericAudioPreprocess, ReconfigurationSwitchesBetweenLogMelAndPcm) {
    Inference inference;
    opk::stdop::GenericAudioPreprocessOp pre;
    opk::PcmRingBuffer pcm(384);
    ASSERT_TRUE(pcm.append(std::vector<float>(384, 0.25f)));
    opk::op::OpChainContext context;
    context.inferenceInfo.audio.hopSampleCount = 128;
    for (const bool logMel : {true, false, true}) {
        ASSERT_TRUE(pre.configure(logMel ? logMelAttributes() : opk::AttributeMap{}));
        EXPECT_EQ(pre.getRequiredAudioSamples(), 0u);
        EXPECT_FALSE(pre.process(context));
        auto &input = inference.model.inputs[0];
        input.dataKind = logMel ? opk::DataKind::AudioLogMel : opk::DataKind::AudioPcm;
        input.shape = logMel ? opk::Shape(1, 1, 24) : opk::Shape(4);
        inference.data.assign(logMel ? 24 : 4, 17.0f);
        ASSERT_TRUE(pre.bind(0, {&pre, &inference}));
        EXPECT_EQ(pre.getRequiredAudioSamples(), logMel ? 384u : 4u);
        const auto window = pcm.latest(pre.getRequiredAudioSamples());
        ASSERT_TRUE(window);
        context.pcmViews.insert_or_assign("pipelineAudioData", *window);
        ASSERT_TRUE(pre.process(context));
        if (logMel)
            EXPECT_TRUE(std::all_of(inference.data.begin(),
                                    inference.data.end(),
                                    [](const float value) { return std::isfinite(value); }));
        else
            EXPECT_EQ(inference.data, std::vector<float>(4, 0.25f));
    }
}
TEST(GenericAudioPreprocess, ValidatesExplicitFrontendParametersAndNormalizationFields) {
    opk::stdop::GenericAudioPreprocessOp pre;
    for (const auto *const name : {"windowSampleCount",
                                   "fftSize",
                                   "windowLengthSamples",
                                   "frameHopSamples",
                                   "minFrequencyHz",
                                   "maxFrequencyHz",
                                   "logOffset",
                                   "layout",
                                   "normalization"}) {
        auto attributes = logMelAttributes();
        attributes.getObject("logMel")->raw().erase(name);
        EXPECT_FALSE(pre.configure(attributes)) << name;
        attributes = logMelAttributes();
        attributes.getObject("logMel")->set(name, true);
        EXPECT_FALSE(pre.configure(attributes)) << name;
    }
    for (const auto *const name :
         {"windowSampleCount", "fftSize", "windowLengthSamples", "frameHopSamples"}) {
        auto attributes = logMelAttributes();
        attributes.getObject("logMel")->set(name, 384.0);
        EXPECT_FALSE(pre.configure(attributes)) << name;
    }
    for (const double offset : {0.0, -1.0, 1e-100, std::numeric_limits<double>::infinity()}) {
        auto attributes = logMelAttributes();
        attributes.getObject("logMel")->set("logOffset", offset);
        EXPECT_FALSE(pre.configure(attributes));
    }
    auto attributes = logMelAttributes();
    attributes.getObject("logMel")->set("layout", "other");
    EXPECT_FALSE(pre.configure(attributes));
    for (const auto *const mode : {"none", "window", "rolling"}) {
        attributes = logMelAttributes(384, 160, mode);
        ASSERT_TRUE(pre.configure(attributes));
        auto norm = attributes.getObject("logMel")->getObject("normalization");
        norm->set("extra", int64_t{1});
        EXPECT_FALSE(pre.configure(attributes));
        norm->raw().erase("extra");
        if (std::string_view(mode) == "none") {
            norm->set("epsilon", 1e-6);
        } else {
            norm->raw().erase("epsilon");
        }
        EXPECT_FALSE(pre.configure(attributes));
    }
    attributes = logMelAttributes(384, 128, "window");
    attributes.getObject("logMel")->getObject("normalization")->set("historyFrames", int64_t{7});
    EXPECT_FALSE(pre.configure(attributes));
    attributes = logMelAttributes();
    attributes.getObject("logMel")->getObject("normalization")->set("historyFrames", 7.0);
    EXPECT_FALSE(pre.configure(attributes));
}

TEST(GenericAudioPreprocess, BindsGenericMelMajorTensorAndPreservesRawFeatureOrder) {
    auto attributes = logMelAttributes(1024, 80, "none");
    auto frontend = attributes.getObject("logMel");
    frontend->set("fftSize", int64_t{256});
    frontend->set("windowLengthSamples", int64_t{201});
    frontend->set("minFrequencyHz", 80.0);
    frontend->set("maxFrequencyHz", 7600.0);
    frontend->set("logOffset", 1e-5);
    Inference inference;
    inference.model.inputs[0].dataKind = opk::DataKind::AudioLogMel;
    inference.model.inputs[0].shape = opk::Shape(1, 10, 23);
    inference.data.resize(230);
    opk::stdop::GenericAudioPreprocessOp pre;
    ASSERT_TRUE(pre.configure(attributes));
    ASSERT_TRUE(pre.bind(0, {&pre, &inference}));
    EXPECT_EQ(pre.getRequiredAudioSamples(), 1024u);
    std::vector<float> samples(1024);
    samples[128] = 1;
    opk::PcmRingBuffer pcm(1024);
    ASSERT_TRUE(pcm.append(samples));
    opk::op::OpChainContext context;
    context.pcmViews.emplace("pipelineAudioData", pcm.view());
    context.inferenceInfo.audio.hopSampleCount = 160;
    ASSERT_TRUE(pre.process(context));
    const auto timeMajor = inference.data;
    frontend->set("layout", "mel-major");
    ASSERT_TRUE(pre.configure(attributes));
    EXPECT_FALSE(pre.bind(0, {&pre, &inference}));
    inference.model.inputs[0].shape = opk::Shape(1, 23, 10);
    ASSERT_TRUE(pre.bind(0, {&pre, &inference}));
    ASSERT_TRUE(pre.process(context));
    for (size_t frame = 0; frame < 10; ++frame) {
        for (size_t bin = 0; bin < 23; ++bin)
            EXPECT_EQ(inference.data[bin * 10 + frame], timeMajor[frame * 23 + bin]);
    }
}
} // namespace
