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

#include "prompt_stage_test_state.h"

#include "mediaio/PixelBufferVideoFrame.h"
#include "op/OpChain.h"
#include "op/OpRef.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

class StagePayload final : public opk::PromptPayload {
  public:
    opk::PromptFormat format() const override {
        return {"TEST", 1, 0};
    }
};

std::shared_ptr<const opk::PromptSnapshot> snapshot(const std::string &target = "prompted") {
    open_perception_kit::metadata::RequestEnvelopeT request;
    request.request_id = 17;
    request.revision = 1;
    request.destination = std::make_unique<open_perception_kit::metadata::RequestDestinationT>();
    request.destination->inference_instance = target;
    request.destination->source_name = "pipelineVideoFrame";
    const auto payload = std::make_shared<const StagePayload>();
    return std::make_shared<const opk::PromptSnapshot>("session", std::move(request), payload);
}

class PromptStageLifecycle : public ::testing::Test {
  protected:
    opk::op::OpChain chain;
    opk::op::OpChainContext context;
    PromptStageTestState prepare;
    PromptStageTestState inference;
    PromptStageTestState observer;

    void add(const std::string &name,
             const std::string &instanceId,
             PromptStageTestState &state,
             const size_t loopId = 0) {
        opk::op::OpRef reference;
        const auto loaded = reference.bind(OPK_STAGE_TEST_OPS_PATH, name);
        ASSERT_TRUE(loaded) << loaded.error().info;
        reference->instanceId = instanceId;
        reference->loopId = loopId;
        opk::AttributeMap attributes;
        attributes.set("stateAddress",
                       static_cast<int64_t>(reinterpret_cast<std::uintptr_t>(&state)));
        const auto configured = reference->configure(attributes);
        ASSERT_TRUE(configured) << configured.error().info;
        chain.add(reference);
    }

    void standardStage() {
        add("Prepare", "prepare", prepare);
        add("Inference", "prompted", inference);
        add("Observe", "observe", observer);
    }

    void expectNoOutputAssociation() const {
        EXPECT_FALSE(context.preparedPrompt);
        EXPECT_FALSE(context.inferencePrompt);
    }
};

TEST_F(PromptStageLifecycle, PostprocessOnlyChainRetainsCallerProvidedTensorViews) {
    add("Observe", "observe", observer);
    ASSERT_TRUE(chain.bind());
    const float value = 0.5F;
    context.inferenceOutputTensorCount = 1;
    context.inferenceOutputTensors[0] =
        opk::TensorView(&value, sizeof(value), opk::Shape(1), opk::Dtype::Float32, 1, 0);
    context.inferencePrompt = snapshot();
    ASSERT_TRUE(chain.execute(context));
    expectNoOutputAssociation();
    EXPECT_EQ(context.inferenceOutputTensorCount, 1U);
    EXPECT_TRUE(context.inferenceOutputTensors[0].isValid());
    EXPECT_FLOAT_EQ(context.inferenceOutputTensors[0].get(0), value);
}

TEST_F(PromptStageLifecycle, RealOcrPreparationCannotInheritEarlierPromptAssociation) {
    standardStage();
    PromptStageTestState crop;
    PromptStageTestState ocrInference;
    PromptStageTestState ocrObserver;
    add("Crop", "crop", crop);
    opk::op::OpRef ocr;
    ASSERT_TRUE(ocr.bind(OPK_STDOPS_PATH, "PaddleOcrPreprocess"));
    opk::AttributeMap attributes;
    attributes.set("mode", "detection");
    attributes.set("paddingValue", 0.0);
    ASSERT_TRUE(ocr->configure(attributes));
    chain.add(ocr);
    add("Inference", "ocr", ocrInference);
    add("Observe", "ocr-observer", ocrObserver);
    ASSERT_TRUE(chain.bind());
    context.prompt = snapshot();
    context.videoFrames["pipelineVideoFrame"] = opk::mediaio::PixelBufferVideoFrame::take(
        std::vector<uint8_t>(2 * 2 * 4, 128), 2, 2, opk::RawImagePixelFormat::Bgra);
    ASSERT_TRUE(chain.execute(context));
    ASSERT_EQ(observer.observed.size(), 1U);
    EXPECT_EQ(observer.observed.front(), context.prompt);
    ASSERT_EQ(ocrObserver.observed.size(), 1U);
    EXPECT_FALSE(ocrObserver.observed.front());
    EXPECT_FALSE(inference.sawAssociationDuringInference);
    EXPECT_FALSE(ocrInference.sawAssociationDuringInference);
    EXPECT_FALSE(ocrObserver.sawPreparedAfterInference);
    expectNoOutputAssociation();
    EXPECT_EQ(context.inferenceOutputTensorCount, 1U);
    EXPECT_TRUE(context.inferenceOutputTensors[0].isValid());
}

TEST_F(PromptStageLifecycle, ReusedInvocationDoesNotReusePreparationOrOutputAssociation) {
    standardStage();
    ASSERT_TRUE(chain.bind());
    context.prompt = snapshot();
    ASSERT_TRUE(chain.execute(context));
    ASSERT_EQ(observer.observed.back(), context.prompt);
    prepare.preparePrompt = false;
    ASSERT_TRUE(chain.execute(context));
    EXPECT_FALSE(observer.observed.back());
    expectNoOutputAssociation();
}

TEST_F(PromptStageLifecycle, RepeatedInferenceConsumesPreparationOnlyOnce) {
    observer.breakAfter = 2;
    add("Prepare", "prepare", prepare, 1);
    add("Inference", "prompted", inference, 1);
    add("Observe", "observe", observer, 1);
    ASSERT_TRUE(chain.bind());
    context.prompt = snapshot();
    ASSERT_TRUE(chain.execute(context));
    EXPECT_EQ(prepare.calls, 1U);
    EXPECT_EQ(inference.calls, 2U);
    ASSERT_EQ(observer.observed.size(), 2U);
    EXPECT_EQ(observer.observed[0], context.prompt);
    EXPECT_FALSE(observer.observed[1]);
    expectNoOutputAssociation();
}

TEST_F(PromptStageLifecycle, WrongInferenceTargetFailsBeforeBackendInvocation) {
    standardStage();
    ASSERT_TRUE(chain.bind());
    context.prompt = snapshot("other");
    EXPECT_FALSE(chain.execute(context));
    EXPECT_EQ(inference.calls, 0U);
    EXPECT_EQ(observer.calls, 0U);
    expectNoOutputAssociation();
    EXPECT_EQ(context.inferenceOutputTensorCount, 0U);
}

TEST_F(PromptStageLifecycle, FailedInferenceDiscardsPartialOutputAndPreparedPrompt) {
    standardStage();
    ASSERT_TRUE(chain.bind());
    context.prompt = snapshot();
    inference.error = true;
    EXPECT_FALSE(chain.execute(context));
    EXPECT_EQ(observer.calls, 0U);
    expectNoOutputAssociation();
    EXPECT_EQ(context.inferenceOutputTensorCount, 0U);
    EXPECT_FALSE(context.inferenceOutputTensors[0].isValid());
    inference.error = false;
    prepare.preparePrompt = false;
    ASSERT_TRUE(chain.execute(context));
    EXPECT_FALSE(observer.observed.back());
}

TEST_F(PromptStageLifecycle, SkippedInferenceDiscardsPartialOutputAndPreparedPrompt) {
    standardStage();
    ASSERT_TRUE(chain.bind());
    context.prompt = snapshot();
    inference.signal = opk::op::OpSignal::BreakLoop;
    ASSERT_TRUE(chain.execute(context));
    ASSERT_EQ(observer.observed.size(), 1U);
    EXPECT_FALSE(observer.observed.back());
    expectNoOutputAssociation();
    EXPECT_EQ(context.inferenceOutputTensorCount, 0U);
    EXPECT_FALSE(context.inferenceOutputTensors[0].isValid());
}

TEST_F(PromptStageLifecycle, EmptyAndInvalidOutputCannotPublishPromptAssociation) {
    standardStage();
    ASSERT_TRUE(chain.bind());
    context.prompt = snapshot();
    inference.produceOutput = false;
    ASSERT_TRUE(chain.execute(context));
    EXPECT_FALSE(observer.observed.back());
    expectNoOutputAssociation();
    inference.produceOutput = true;
    inference.validOutput = false;
    ASSERT_TRUE(chain.execute(context));
    EXPECT_FALSE(observer.observed.back());
    expectNoOutputAssociation();
}

TEST_F(PromptStageLifecycle, AbortedPreparationClearsOldOutputsAndDoesNotRunInference) {
    standardStage();
    ASSERT_TRUE(chain.bind());
    context.prompt = snapshot();
    ASSERT_TRUE(chain.execute(context));
    ASSERT_TRUE(context.inferencePrompt);
    prepare.signal = opk::op::OpSignal::AbortChain;
    ASSERT_TRUE(chain.execute(context));
    EXPECT_EQ(inference.calls, 1U);
    expectNoOutputAssociation();
    EXPECT_EQ(context.inferenceOutputTensorCount, 0U);
    EXPECT_FALSE(context.inferenceOutputTensors[0].isValid());
}

} // namespace
