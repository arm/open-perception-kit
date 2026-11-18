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

#include "BoxPromptPreprocessOp.h"
#include "GenericImagePreprocessOp.h"
#include "mediaio/PixelBufferVideoFrame.h"
#include "opk/BoxPromptInputs.h"
#include "postproc/MobileSamMaskParser.h"
#include "preproc/GenericImageTensorBuilder.h"
#include "prompt_reference.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace {

using namespace open_perception_kit::metadata;
constexpr uint64_t ParentId = 42;

class FakeInference : public opk::op::Op,
                      public opk::op::OpInterfaceInference,
                      public opk::op::OpInterfaceSharedInput {
  public:
    FakeInference() {
        instanceId = "mobileSam";
        opk::ModelInput image;
        image.dataKind = opk::DataKind::ImageRgbChw;
        image.valueType = opk::Dtype::Float32;
        image.shape = opk::Shape(1, 3, 1024, 1024);
        opk::ModelInput box;
        box.valueType = opk::Dtype::Float32;
        box.shape = opk::Shape(1, 1, 2, 2);
        model.name = "box-test";
        model.inputs = {image, box};
        imageValues.resize(3 * 1024 * 1024);
        boxValues.fill(-123.0F);
        oldBoxValues.fill(-42.0F);
    }

    opk::Result<void> configure(const opk::AttributeMap &) override {
        return {};
    }
    opk::Result<void> bind(const size_t, const std::vector<opk::op::Op *> &) override {
        return {};
    }
    opk::Result<opk::op::OpSignal> process(opk::op::OpChainContext &) override {
        return opk::op::OpSignal::Continue;
    }
    const opk::Model &getModel() const override {
        return model;
    }
    uint8_t *getTensorDataAddress(const size_t index) const override {
        if (missingStorage) {
            return nullptr;
        }
        return reinterpret_cast<uint8_t *>(
            index == 0 ? imageValues.data() : (ownedBox ? boxValues.data() : oldBoxValues.data()));
    }
    opk::Result<void>
    setSharedInputStorage(const size_t, const std::span<uint8_t>, std::shared_ptr<void>) override {
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::NotSupported, "test"));
    }
    opk::Result<void> setOwnedInputStorage(const size_t index) override {
        if (index == 1) {
            ownedBox = true;
        }
        return {};
    }

    opk::Model model;
    mutable std::vector<float> imageValues;
    mutable std::array<float, 4> boxValues;
    mutable std::array<float, 4> oldBoxValues;
    bool ownedBox{false};
    bool missingStorage{false};
};

// Binding-only stand-in; no synthetic input writer is used for model execution.
class AuxiliaryInputPreprocessor final : public opk::op::Op,
                                         public opk::op::OpInterfaceInferenceInputPreprocessor {
  public:
    opk::Result<void> configure(const opk::AttributeMap &) override {
        return {};
    }
    opk::Result<void> bind(const size_t, const std::vector<opk::op::Op *> &) override {
        return {};
    }
    opk::Result<opk::op::OpSignal> process(opk::op::OpChainContext &) override {
        return opk::op::OpSignal::Continue;
    }
    opk::op::InferenceInputBinding getInputBinding() const override {
        return {target, source, inputs};
    }

    std::string target = "mobileSam";
    std::string source = "pipelineVideoFrame";
    std::vector<size_t> inputs{1};
};

struct BoxFixture {
    FakeInference inference;
    opk::stdop::GenericImagePreprocessOp image;
    opk::stdop::BoxPromptPreprocessOp box;
    opk::op::OpChainContext context;
    open_perception_kit::FrameResults results;
    std::shared_ptr<opk::PromptSnapshot> snapshot;

    PromptBoxT geometry;

    void publishBox() {
        snapshot->payload = std::make_shared<const opk::BoxPromptPayload>(geometry);
    }

    BoxFixture() {
        opk::AttributeMap imageAttributes;
        imageAttributes.set("inferenceInstance", "mobileSam");
        imageAttributes.set("inputImageTensorIndex", std::int64_t{0});
        EXPECT_TRUE(image.configure(imageAttributes));
        opk::AttributeMap boxAttributes;
        boxAttributes.set("inferenceInstance", "mobileSam");
        boxAttributes.set("inputBoxTensorIndex", std::int64_t{1});
        EXPECT_TRUE(box.configure(boxAttributes));
        EXPECT_TRUE(image.bind(0, {&image, &box, &inference}));
        EXPECT_TRUE(box.bind(1, {&image, &box, &inference}));
        RequestEnvelopeT prompt;
        prompt.request_id = std::numeric_limits<uint64_t>::max() - 10;
        prompt.revision = 1;
        prompt.destination = std::make_unique<RequestDestinationT>();
        prompt.destination->inference_instance = "mobileSam";
        prompt.destination->source_name = "pipelineVideoFrame";
        snapshot = std::make_shared<opk::PromptSnapshot>("controller", std::move(prompt), nullptr);
        geometry.left = 0.25;
        geometry.top = 0.125;
        geometry.right = 0.75;
        geometry.bottom = 1.0;
        publishBox();
        context.prompt = snapshot;
        context.videoFrames["pipelineVideoFrame"] = opk::mediaio::PixelBufferVideoFrame::take(
            std::vector<uint8_t>(4 * 8 * 4, 128), 8, 4, opk::RawImagePixelFormat::Bgra);
        context.inferenceInfo.parentId = ParentId;
        context.inferenceInfo.image = {
            .width = 8, .height = 4, .modelWidth = 1024, .modelHeight = 1024};
        FrameContextT frame;
        frame.video = std::make_unique<VideoFrameContextT>();
        frame.video->object = open_perception_kit::makeObjectMeta(ParentId);
        frame.video->original_width = 8;
        frame.video->original_height = 4;
        results.add(std::move(frame));
        context.frameResults = &results;
    }
};

TEST(BoxPromptPreprocess, WritesAllCornersToFreshStorageInModelSpace) {
    BoxFixture fixture;
    const auto processed = fixture.box.process(fixture.context);
    ASSERT_TRUE(processed);
    EXPECT_EQ(*processed, opk::op::OpSignal::Continue);
    EXPECT_EQ(fixture.inference.boxValues, (std::array<float, 4>{256, 128, 768, 1024}));
    EXPECT_EQ(fixture.inference.oldBoxValues, (std::array<float, 4>{-42, -42, -42, -42}));
    fixture.geometry.left = 0;
    fixture.geometry.top = 0;
    fixture.geometry.right = 1;
    fixture.publishBox();
    ASSERT_TRUE(fixture.box.process(fixture.context));
    EXPECT_EQ(fixture.inference.boxValues, (std::array<float, 4>{0, 0, 1024, 1024}));
}

class UnsupportedPayload final : public opk::PromptPayload {
  public:
    explicit UnsupportedPayload(const opk::PromptFormat format) : value(format) {}
    opk::PromptFormat format() const override {
        return value;
    }

  private:
    const opk::PromptFormat value;
};

TEST(BoxPromptPreprocess, RejectsUnsupportedFormatVersionAndForgedConcreteType) {
    BoxFixture fixture;
    EXPECT_TRUE(fixture.box.acceptsPrompt(opk::BoxPromptFormat, "mobileSam", "pipelineVideoFrame"));
    EXPECT_FALSE(fixture.box.acceptsPrompt({"TEXT", 1, 0}, "mobileSam", "pipelineVideoFrame"));
    EXPECT_FALSE(fixture.box.acceptsPrompt({"BPRM", 1, 1}, "mobileSam", "pipelineVideoFrame"));
    for (const opk::PromptFormat format :
         {opk::PromptFormat{"TEXT", 1, 0}, opk::PromptFormat{"BPRM", 1, 1}, opk::BoxPromptFormat}) {
        fixture.context.preparedPrompt = fixture.context.prompt;
        fixture.snapshot->payload = std::make_shared<const UnsupportedPayload>(format);
        EXPECT_FALSE(fixture.box.process(fixture.context));
        EXPECT_FALSE(fixture.context.preparedPrompt);
        EXPECT_FALSE(fixture.inference.ownedBox);
    }
}

TEST(BoxPromptPreprocess, ImagePreparationDoesNotOwnOutputPromptAssociation) {
    BoxFixture fixture;
    fixture.context.inferencePrompt = fixture.context.prompt;
    fixture.context.inferenceImageCrops = {{0, 0, 8, 4}};
    fixture.context.inferenceImageCropIds = {ParentId};
    ASSERT_TRUE(fixture.image.process(fixture.context));
    EXPECT_EQ(fixture.context.inferencePrompt, fixture.context.prompt);
}

TEST(BoxPromptPreprocess, OptionalPayloadSkipsAndClearsPendingPrompt) {
    BoxFixture fixture;
    fixture.context.inferenceOutputTensorCount = 2;
    fixture.context.preparedPrompt = fixture.context.prompt;
    fixture.snapshot->payload.reset();
    const auto absent = fixture.box.process(fixture.context);
    ASSERT_TRUE(absent);
    EXPECT_EQ(*absent, opk::op::OpSignal::AbortChain);
    EXPECT_FALSE(fixture.context.preparedPrompt);
    fixture.context.prompt.reset();
    const auto noMeta = fixture.box.process(fixture.context);
    ASSERT_TRUE(noMeta);
    EXPECT_EQ(*noMeta, opk::op::OpSignal::AbortChain);
    EXPECT_EQ(fixture.inference.boxValues, (std::array<float, 4>{-123, -123, -123, -123}));
}

TEST(BoxPromptPreprocess, RejectsBoxThatCollapsesWhenPackedAsFloat32) {
    BoxFixture fixture;
    auto &box = fixture.geometry;
    box.left = 0.5;
    box.right = 0.5 + std::numeric_limits<double>::epsilon();
    fixture.publishBox();
    EXPECT_FALSE(fixture.box.process(fixture.context));
    EXPECT_FALSE(fixture.inference.ownedBox);
    EXPECT_EQ(fixture.inference.boxValues, (std::array<float, 4>{-123, -123, -123, -123}));
}

TEST(BoxPromptPreprocess, RejectsMalformedPresentDataAndMisrouting) {
    BoxFixture fixture;
    auto &box = fixture.geometry;
    for (const double invalid : {-0.1,
                                 0.75,
                                 2.0,
                                 std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::infinity()}) {
        box.left = invalid;
        fixture.publishBox();
        EXPECT_FALSE(fixture.box.process(fixture.context));
        EXPECT_EQ(fixture.inference.boxValues[0], -123);
    }
    box.left = 0.25;
    fixture.publishBox();
    auto rerouted = fixture.snapshot->copyRequest();
    rerouted.destination->inference_instance = "other";
    fixture.context.prompt = std::make_shared<const opk::PromptSnapshot>(
        "controller", std::move(rerouted), fixture.snapshot->payload);
    EXPECT_FALSE(fixture.box.process(fixture.context));
    fixture.context.prompt = fixture.snapshot;
    fixture.context.inferenceInfo.image.letterboxLeft = 1;
    EXPECT_FALSE(fixture.box.process(fixture.context));
    fixture.context.inferenceInfo.image.letterboxLeft = 0;
    fixture.context.inferenceInfo.image.width = 4;
    EXPECT_FALSE(fixture.box.process(fixture.context));
    fixture.context.inferenceInfo.image.width = 8;
    fixture.inference.missingStorage = true;
    EXPECT_FALSE(fixture.box.process(fixture.context));
}

TEST(BoxPromptPreprocess, RejectsConflictingWritersLoopsAndUnsupportedInterveningOps) {
    BoxFixture fixture;
    opk::AttributeMap wrongInput;
    wrongInput.set("inferenceInstance", "mobileSam");
    wrongInput.set("inputBoxTensorIndex", std::int64_t{0});
    ASSERT_TRUE(fixture.box.configure(wrongInput));
    EXPECT_FALSE(fixture.box.bind(1, {&fixture.image, &fixture.box, &fixture.inference}));
    fixture.image.loopId = 1;
    EXPECT_FALSE(fixture.image.bind(0, {&fixture.image, &fixture.box, &fixture.inference}));
    fixture.image.loopId = 0;
    opk::stdop::GenericImagePreprocessOp intervening;
    EXPECT_FALSE(fixture.image.bind(0, {&fixture.image, &intervening, &fixture.inference}));
}

TEST(BoxPromptPreprocess, BindingRejectsDeferredDestinationSourceAndTensorConflicts) {
    BoxFixture fixture;
    const auto configure =
        [&](const std::string &target, const std::string &source, const int64_t inputIndex) {
            opk::AttributeMap attributes;
            attributes.set("inferenceInstance", target);
            attributes.set("inputImageSourceName", source);
            attributes.set("inputBoxTensorIndex", inputIndex);
            return fixture.box.configure(attributes);
        };
    ASSERT_TRUE(configure("other", "pipelineVideoFrame", 1));
    EXPECT_FALSE(fixture.box.bind(1, {&fixture.image, &fixture.box, &fixture.inference}));
    EXPECT_FALSE(fixture.image.bind(0, {&fixture.image, &fixture.box, &fixture.inference}));
    ASSERT_TRUE(configure("mobileSam", "other", 1));
    EXPECT_FALSE(fixture.box.bind(1, {&fixture.image, &fixture.box, &fixture.inference}));
    EXPECT_FALSE(fixture.image.bind(0, {&fixture.image, &fixture.box, &fixture.inference}));
    for (const int64_t inputIndex : {0, 2}) {
        ASSERT_TRUE(configure("mobileSam", "pipelineVideoFrame", inputIndex));
        EXPECT_FALSE(fixture.box.bind(1, {&fixture.image, &fixture.box, &fixture.inference}));
        EXPECT_FALSE(fixture.image.bind(0, {&fixture.image, &fixture.box, &fixture.inference}));
    }
    ASSERT_TRUE(configure("mobileSam", "pipelineVideoFrame", 1));
    EXPECT_TRUE(fixture.box.bind(1, {&fixture.image, &fixture.box, &fixture.inference}));
    EXPECT_TRUE(fixture.image.bind(0, {&fixture.image, &fixture.box, &fixture.inference}));
}

TEST(BoxPromptPreprocess, BindingRejectsDeferredSequenceErrorsBeforeInference) {
    BoxFixture fixture;
    AuxiliaryInputPreprocessor auxiliary;
    EXPECT_FALSE(
        fixture.box.bind(1, {&fixture.image, &fixture.box, &auxiliary, &fixture.inference}));
    EXPECT_FALSE(
        fixture.box.bind(2, {&fixture.image, &auxiliary, &fixture.box, &fixture.inference}));
    EXPECT_FALSE(fixture.box.bind(2, {&fixture.image, &fixture.inference, &fixture.box}));
}

TEST(GenericImageBinding, ExistingAdjacencyStillWorksAndInputRangeIsChecked) {
    FakeInference inference;
    opk::stdop::GenericImagePreprocessOp image;
    opk::AttributeMap attributes;
    attributes.set("inputImageTensorIndex", std::int64_t{0});
    ASSERT_TRUE(image.configure(attributes));
    EXPECT_TRUE(image.bind(0, {&image, &inference}));
    attributes.set("inferenceInstance", "mobileSam");
    ASSERT_TRUE(image.configure(attributes));
    EXPECT_TRUE(image.bind(0, {&image, &inference}));
    image.loopId = 1;
    EXPECT_FALSE(image.bind(0, {&image, &inference}));
    image.loopId = 0;
    attributes.set("inputImageTensorIndex", std::int64_t{15});
    ASSERT_TRUE(image.configure(attributes));
    EXPECT_FALSE(image.bind(0, {&image, &inference}));
    attributes.set("inputImageTensorIndex", std::int64_t{-1});
    EXPECT_FALSE(image.configure(attributes));
    attributes.set("inputImageTensorIndex", "0");
    EXPECT_FALSE(image.configure(attributes));
}

TEST(GenericImageBinding, AcceptsNonBoxInputPreprocessorsAndMultipleDisjointWriters) {
    FakeInference inference;
    const auto auxiliaryInput = inference.model.inputs[1];
    inference.model.inputs.push_back(auxiliaryInput);
    opk::stdop::GenericImagePreprocessOp image;
    opk::AttributeMap attributes;
    attributes.set("inferenceInstance", "mobileSam");
    ASSERT_TRUE(image.configure(attributes));
    AuxiliaryInputPreprocessor first;
    AuxiliaryInputPreprocessor second;
    second.inputs = {2};
    EXPECT_TRUE(image.bind(0, {&image, &first, &inference}));
    EXPECT_TRUE(image.bind(0, {&image, &first, &second, &inference}));
    EXPECT_EQ(image.imageTensorIndex(), 0U);

    second.inputs = {1};
    EXPECT_FALSE(image.bind(0, {&image, &first, &second, &inference}));
}

TEST(GenericImageBinding, RejectsMisroutedUndeclaredOutOfRangeAndDuplicateInputWriters) {
    FakeInference inference;
    opk::stdop::GenericImagePreprocessOp image;
    opk::AttributeMap attributes;
    attributes.set("inferenceInstance", "mobileSam");
    ASSERT_TRUE(image.configure(attributes));
    AuxiliaryInputPreprocessor auxiliary;
    const std::vector<opk::op::Op *> ops{&image, &auxiliary, &inference};
    auxiliary.target = "other";
    EXPECT_FALSE(image.bind(0, ops));
    auxiliary.target = "mobileSam";
    auxiliary.source = "other";
    EXPECT_FALSE(image.bind(0, ops));
    auxiliary.source = "pipelineVideoFrame";
    const std::array<std::vector<size_t>, 5> invalidInputs{
        {{}, {0}, {2}, {1, 1}, {opk::InvalidTensorIndex}}};
    for (const auto &inputs : invalidInputs) {
        auxiliary.inputs = inputs;
        EXPECT_FALSE(image.bind(0, ops));
    }
    auxiliary.inputs = {1};
    EXPECT_TRUE(image.bind(0, ops));
}

TEST(GenericImageBinding, RejectsInterveningLoopsInferenceAndMissingDownstreamTargets) {
    FakeInference inference;
    opk::stdop::GenericImagePreprocessOp image;
    opk::AttributeMap attributes;
    attributes.set("inferenceInstance", "mobileSam");
    ASSERT_TRUE(image.configure(attributes));
    AuxiliaryInputPreprocessor auxiliary;
    auxiliary.loopId = 1;
    EXPECT_FALSE(image.bind(0, {&image, &auxiliary, &inference}));
    auxiliary.loopId = 0;
    inference.loopId = 1;
    EXPECT_FALSE(image.bind(0, {&image, &auxiliary, &inference}));
    inference.loopId = 0;
    FakeInference intervening;
    intervening.instanceId = "other";
    EXPECT_FALSE(image.bind(0, {&image, &intervening, &auxiliary, &inference}));
    EXPECT_FALSE(image.bind(0, {&image, &auxiliary}));
    EXPECT_FALSE(image.bind(1, {&inference, &image, &auxiliary}));
    auxiliary.instanceId = "mobileSam";
    EXPECT_FALSE(image.bind(0, {&image, &auxiliary, &inference}));
}

TEST(PromptImageSampling, UsesExistingNearestNeighbourImageBuilder) {
    const std::array<uint8_t, 8> pixels{0, 0, 0, 255, 255, 255, 255, 255};
    std::array<float, 12> output{};
    opk::ImageOpDesc source{};
    source.surfaceWidth = 2;
    source.surfaceHeight = 1;
    source.rect = {0, 0, 2, 1};
    source.format = opk::RawImagePixelFormat::Bgra;
    source.type = opk::Dtype::Uint8;
    source.planeCount = 1;
    source.planes[0] = {pixels.data(), nullptr, pixels.size(), 8};
    opk::ImageOpDesc destination{};
    destination.surfaceWidth = 4;
    destination.surfaceHeight = 1;
    destination.rect = {0, 0, 4, 1};
    destination.type = opk::Dtype::Float32;
    destination.kind = opk::DataKind::ImageRgbChw;
    destination.planeCount = 1;
    destination.planes[0] = {
        nullptr, reinterpret_cast<uint8_t *>(output.data()), sizeof(output), 0};
    opk::stdop::preproc::GenericImageTensorBuilder builder;
    ASSERT_TRUE(builder.build({source, destination}));
    const std::array<float, 4> expected{0, 0, 1, 1};
    for (size_t channel = 0; channel < 3; ++channel) {
        for (size_t x = 0; x < expected.size(); ++x) {
            EXPECT_FLOAT_EQ(output[channel * 4 + x], expected[x]);
        }
    }
}

TEST(BoxPromptPreprocess, ImageOpRecordsActualStretchBeforePackingBox) {
    BoxFixture fixture;
    fixture.context.inferenceImageCrops = {{0, 0, 8, 4}};
    fixture.context.inferenceImageCropIds = {ParentId};
    const auto imageResult = fixture.image.process(fixture.context);
    ASSERT_TRUE(imageResult);
    EXPECT_EQ(*imageResult, opk::op::OpSignal::Continue);
    EXPECT_EQ(fixture.context.inferenceInfo.image.width, 8);
    EXPECT_EQ(fixture.context.inferenceInfo.image.height, 4);
    EXPECT_EQ(fixture.context.inferenceInfo.image.modelWidth, 1024);
    EXPECT_EQ(fixture.context.inferenceInfo.image.letterboxLeft, 0);
    EXPECT_NEAR(fixture.inference.imageValues.front(), 128.0F / 255.0F, 1e-7F);
    EXPECT_NEAR(fixture.inference.imageValues.back(), 128.0F / 255.0F, 1e-7F);
    ASSERT_TRUE(fixture.box.process(fixture.context));
    EXPECT_EQ(fixture.inference.boxValues, (std::array<float, 4>{256, 128, 768, 1024}));
}

struct MaskFixture {
    std::vector<float> logits = std::vector<float>(3 * 256 * 256, -1.0F);
    std::array<float, 3> scores{0.5F, 0.9F, 0.6F};
    opk::TensorView logitView{logits.data(),
                              logits.size() * sizeof(float),
                              opk::Shape(1, 3, 256, 256),
                              opk::Dtype::Float32,
                              1,
                              0};
    opk::TensorView scoreView{
        scores.data(), sizeof(scores), opk::Shape(1, 3), opk::Dtype::Float32, 1, 0};
    opk::AttributeMap attributes;
    opk::TensorParser::Input input{attributes};
    open_perception_kit::FrameResults output;
    opk::stdop::postproc::MobileSamMaskParser parser;

    explicit MaskFixture(const size_t width = 7, const size_t height = 3) {
        input.tensors[0] = &logitView;
        input.tensors[1] = &scoreView;
        input.inferenceInfo.parentId = ParentId;
        input.inferenceInfo.image = {
            .width = width, .height = height, .modelWidth = 1024, .modelHeight = 1024};
        FrameContextT frame;
        frame.video = std::make_unique<VideoFrameContextT>();
        frame.video->object = open_perception_kit::makeObjectMeta(ParentId);
        frame.video->original_width = width;
        frame.video->original_height = height;
        output.add(std::move(frame));
    }
};

TEST(PromptMaskDecode, AssociatesOnlyConsumedPromptWithNewResultIncludingEmptyMask) {
    MaskFixture fixture;
    BoxFixture box;
    ASSERT_TRUE(box.box.process(box.context));
    fixture.input.prompt = box.context.preparedPrompt;
    InstanceSegmentationMasksT unrelated;
    unrelated.layer = open_perception_kit::makeLayerInfo({.model = "unrelated"});
    fixture.output.add(std::move(unrelated));
    ASSERT_TRUE(fixture.parser.parse(fixture.input, fixture.output));
    EXPECT_EQ(fixture.output.count<InstanceSegmentationMasksT>(), 2U);
    EXPECT_EQ(fixture.output.count<PromptAssociationT>(), 1U);
    fixture.output.for_each<PromptAssociationT>([&](const auto &association) {
        EXPECT_EQ(association.session_id, box.snapshot->sessionId);
        EXPECT_EQ(association.request->request_id, box.snapshot->requestId());
        EXPECT_EQ(association.request->revision, 1U);
        EXPECT_EQ(association.parent_id, ParentId);
        EXPECT_EQ(association.layer->content_type, "instanceSegmentation");
        EXPECT_NE(association.layer->model, "unrelated");
    });
    fixture.input.prompt.reset();
    ASSERT_TRUE(fixture.parser.parse(fixture.input, fixture.output));
    EXPECT_EQ(fixture.output.count<PromptAssociationT>(), 1U);
    box.snapshot->payload =
        std::make_shared<const UnsupportedPayload>(opk::PromptFormat{"TEXT", 1, 0});
    fixture.input.prompt = box.snapshot;
    ASSERT_TRUE(fixture.parser.parse(fixture.input, fixture.output));
    EXPECT_EQ(fixture.output.count<InstanceSegmentationMasksT>(), 4U);
    EXPECT_EQ(fixture.output.count<PromptAssociationT>(), 2U);
}

TEST(PromptMaskDecode, BestIoUAndHalfPixelThresholdThenNearestRestore) {
    MaskFixture fixture;
    for (size_t y = 0; y < 256; ++y) {
        for (size_t x = 128; x < 256; ++x) {
            fixture.logits[256 * 256 + y * 256 + x] = 1;
        }
    }
    ASSERT_TRUE(fixture.parser.parse(fixture.input, fixture.output));
    ASSERT_EQ(fixture.output.count<InstanceSegmentationMasksT>(), 1U);
    fixture.output.for_each<InstanceSegmentationMasksT>([&](const auto &payload) {
        ASSERT_EQ(payload.masks.size(), 1);
        const auto &mask = *payload.masks[0];
        EXPECT_EQ(mask.class_id, -1);
        EXPECT_FLOAT_EQ(mask.confidence, 0.9F);
        EXPECT_EQ(mask.object->parent_id, ParentId);
        EXPECT_EQ(mask.bitmap->width, 7);
        EXPECT_EQ(mask.bitmap->height, 3);
        const std::vector<uint8_t> expected{0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0,
                                            1, 1, 1, 0, 0, 0, 0, 1, 1, 1};
        EXPECT_EQ(mask.bitmap->pixels, expected);
        ASSERT_NE(mask.box, nullptr);
        EXPECT_EQ(mask.box->x, 4);
        EXPECT_EQ(mask.box->width, 3);
    });
}

TEST(PromptMaskDecode, ThresholdIn1024SpaceBeforeNativeNearestIsRequired) {
    MaskFixture fixture;
    // Only the first logit column is background. Native downsampling must keep
    // the top-left label; directly resizing logits to width 7 would erase it.
    for (size_t y = 0; y < 256; ++y) {
        for (size_t x = 1; x < 256; ++x) {
            fixture.logits[256 * 256 + y * 256 + x] = 1.0F;
        }
    }
    ASSERT_TRUE(fixture.parser.parse(fixture.input, fixture.output));
    ASSERT_EQ(fixture.output.count<InstanceSegmentationMasksT>(), 1U);
    fixture.output.for_each<InstanceSegmentationMasksT>([](const auto &payload) {
        const std::vector<uint8_t> expected{0, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1,
                                            1, 1, 1, 0, 1, 1, 1, 1, 1, 1};
        EXPECT_EQ(payload.masks[0]->bitmap->pixels, expected);
    });
}

TEST(PromptMaskDecode, FirstMaximumWinsAndZeroIsBackground) {
    MaskFixture fixture;
    fixture.scores = {0.9F, 0.9F, 0.1F};
    std::fill(fixture.logits.begin(), fixture.logits.begin() + 256 * 256, 0.0F);
    std::fill(fixture.logits.begin() + 256 * 256, fixture.logits.begin() + 2 * 256 * 256, 1.0F);
    ASSERT_TRUE(fixture.parser.parse(fixture.input, fixture.output));
    ASSERT_EQ(fixture.output.count<InstanceSegmentationMasksT>(), 1U);
    fixture.output.for_each<InstanceSegmentationMasksT>([](const auto &payload) {
        EXPECT_EQ(payload.masks[0]->bitmap->pixels, std::vector<uint8_t>(21, 0));
        EXPECT_EQ(payload.masks[0]->box, nullptr);
    });
}

TEST(PromptMaskDecode, MatchesIndependentPytorchBinaryReferenceExactly) {
    MaskFixture fixture(13, 7);
    for (size_t y = 0; y < 256; ++y) {
        for (size_t x = 0; x < 256; ++x) {
            fixture.logits[256 * 256 + y * 256 + x] =
                static_cast<float>(static_cast<int>((x * 13 + y * 7) % 23) - 11);
        }
    }
    ASSERT_TRUE(fixture.parser.parse(fixture.input, fixture.output));
    ASSERT_EQ(fixture.output.count<InstanceSegmentationMasksT>(), 1U);
    fixture.output.for_each<InstanceSegmentationMasksT>([](const auto &payload) {
        EXPECT_EQ(payload.masks[0]->bitmap->pixels,
                  (std::vector<uint8_t>{prompt_reference::NativeMask.begin(),
                                        prompt_reference::NativeMask.end()}));
    });
}

TEST(PromptMaskDecode, RejectsMalformedOutputsAndUnsupportedGeometryWithoutAppending) {
    MaskFixture fixture;
    fixture.scores[1] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(fixture.parser.parse(fixture.input, fixture.output));
    fixture.scores[1] = 0.9;
    fixture.logits[256 * 256] = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(fixture.parser.parse(fixture.input, fixture.output));
    fixture.logits[256 * 256] = -1;
    fixture.input.inferenceInfo.image.letterboxTop = 1;
    EXPECT_FALSE(fixture.parser.parse(fixture.input, fixture.output));
    fixture.input.inferenceInfo.image.letterboxTop = 0;
    fixture.input.tensors[0] = nullptr;
    EXPECT_FALSE(fixture.parser.parse(fixture.input, fixture.output));
    size_t count = 0;
    fixture.output.for_each<InstanceSegmentationMasksT>([&](const auto &) { ++count; });
    EXPECT_EQ(count, 0);
}

} // namespace
