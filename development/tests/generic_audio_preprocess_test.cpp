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
#include <array>
#include <gtest/gtest.h>

namespace {
class Inference : public opk::op::Op, public opk::op::OpInterfaceInference {
  public:
    opk::Model model;
    mutable std::array<float, 4> data{};
    bool missing = false;
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
    uint8_t *getTensorDataAddress(size_t) const override {
        return missing ? nullptr : reinterpret_cast<uint8_t *>(data.data());
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
    ctx.pcmViews.emplace("pipelineAudioData", *ring.window(3, 4));
    ctx.inferenceInfo.audio.hopSampleCount = 2;
    ctx.inferenceInfo.audio.windowStartTimestampNs = 0;
    ctx.inferenceInfo.audio.discontinuity = true;
    ASSERT_TRUE(pre.process(ctx));
    EXPECT_EQ(inference.data, (std::array<float, 4>{3, 4, 5, 6}));
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
    ctx.pcmViews.insert_or_assign("pipelineAudioData", *ring.latest(3));
    EXPECT_FALSE(pre.process(ctx));
    ctx.pcmViews.insert_or_assign("pipelineAudioData", *ring.latest(4));
    inference.missing = true;
    EXPECT_FALSE(pre.process(ctx));
    EXPECT_EQ(inference.data, (std::array<float, 4>{}));
}

TEST(GenericAudioPreprocess, ValidatesBindingAndTensorContract) {
    Inference inference;
    opk::stdop::GenericAudioPreprocessOp pre;
    EXPECT_FALSE(pre.bind(0, {}));
    EXPECT_FALSE(pre.bind(0, {&pre}));
    EXPECT_FALSE(pre.bind(0, {&pre, &pre}));
    auto dynamicShape = opk::Shape(1, 4);
    dynamicShape.dims[1] = -1;
    for (auto shape : {opk::Shape(), opk::Shape(2, 4), dynamicShape}) {
        inference.model.inputs[0].shape = shape;
        EXPECT_FALSE(pre.bind(0, {&pre, &inference}));
    }
    inference.model.inputs[0].shape = opk::Shape(4);
    inference.model.inputs[0].valueType = opk::Dtype::Uint8;
    EXPECT_FALSE(pre.bind(0, {&pre, &inference}));
    inference.model.inputs[0].valueType = opk::Dtype::Float32;
    inference.model.inputs[0].dataKind = opk::DataKind::AudioLogMel;
    EXPECT_FALSE(pre.bind(0, {&pre, &inference}));
    inference.model.inputs[0].dataKind = opk::DataKind::AudioPcm;
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
    EXPECT_EQ(inference.data, (std::array<float, 4>{1, 2, 3, 4}));
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
    ctx.pcmViews.emplace("pipelineAudioData", *ring.latest(3));
    ASSERT_TRUE(pre.process(ctx));
    EXPECT_EQ(inference.data, (std::array<float, 4>{0, 1, 2, 3}));
    ASSERT_TRUE(ring.append(std::array<float, 3>{4, 5, 6}));
    ctx.pcmViews.insert_or_assign("pipelineAudioData", *ring.latest(3));
    ctx.inferenceInfo.audio.discontinuity = false;
    ASSERT_TRUE(pre.process(ctx));
    EXPECT_EQ(inference.data, (std::array<float, 4>{3, 4, 5, 6}));
    EXPECT_FALSE(pre.process(ctx)); // Duplicate window must not advance model context.
    ctx.inferenceInfo.audio.discontinuity = true;
    ASSERT_TRUE(pre.process(ctx));
    EXPECT_EQ(inference.data, (std::array<float, 4>{0, 4, 5, 6}));
    ctx.inferenceInfo.audio.hopSampleCount = 2;
    EXPECT_FALSE(pre.process(ctx));
    attrs.set("contextSampleCount", int64_t{4});
    ASSERT_TRUE(pre.configure(attrs));
    EXPECT_FALSE(pre.bind(0, {&pre, &inference}));
    attrs.set("contextSampleCount", int64_t{-1});
    EXPECT_FALSE(pre.configure(attrs));
}
} // namespace
