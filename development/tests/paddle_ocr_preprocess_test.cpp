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

#include "PaddleOcrPreprocessOp.h"
#include "mediaio/PixelBufferVideoFrame.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <vector>

namespace {

class FakeInference : public opk::op::Op, public opk::op::OpInterfaceInference {
  public:
    explicit FakeInference(bool detection = false) {
        model.name = "test-paddle";
        model.contentType = "test-text";
        opk::ModelInput input;
        input.shape = detection ? opk::Shape(1, 3, 736, 736) : opk::Shape(1, 3, 48, 320);
        input.dataKind = opk::DataKind::ImageBgrChw;
        input.mean = detection ? opk::Colorf{0.485f, 0.456f, 0.406f, 0.0f}
                               : opk::Colorf{0.5f, 0.5f, 0.5f, 0.0f};
        input.std = detection ? opk::Colorf{0.229f, 0.224f, 0.225f, 1.0f}
                              : opk::Colorf{0.5f, 0.5f, 0.5f, 1.0f};
        model.inputs.push_back(input);
        // Guards also verify that preprocessing writes only the selected tensor.
        values.assign(input.shape.getFullValueCount() + 2, 42.0f);
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
    uint8_t *getTensorDataAddress(size_t index) const override {
        lastInputIndex = index;
        return missingBuffer ? nullptr : reinterpret_cast<uint8_t *>(values.data() + 1);
    }
    float at(size_t channel, size_t x, size_t y) const {
        const size_t width = model.inputs.back().shape.dims[3];
        const size_t height = model.inputs.back().shape.dims[2];
        return values[1 + channel * width * height + y * width + x];
    }
    opk::Model model;
    mutable std::vector<float> values;
    mutable size_t lastInputIndex = opk::InvalidTensorIndex;
    bool missingBuffer = false;
};

// Deliberately allows malformed plane descriptions to exercise runtime validation.
class TestFrame : public opk::mediaio::VideoFrame {
  public:
    TestFrame(uint32_t width, uint32_t height) : w(width), h(height), stride(width * 4 + 7) {
        pixels.assign(stride * height, 0xcd);
        for (size_t y = 0; y < height; ++y) {
            for (size_t x = 0; x < width; ++x) {
                setPixel(x, y, {10, 100, 240, static_cast<uint8_t>(x + y)});
            }
        }
        views.push_back(opk::mediaio::DataView::host(
            pixels.data(), pixels.size(), stride, opk::AccessMode::Read));
    }
    void setPixel(size_t x, size_t y, const std::array<uint8_t, 4> &pixel) {
        std::copy(pixel.begin(), pixel.end(), pixels.begin() + y * stride + x * 4);
    }
    opk::RawImagePixelFormat format() const noexcept override {
        return pixelFormat;
    }
    uint32_t width() const noexcept override {
        return w;
    }
    uint32_t height() const noexcept override {
        return h;
    }
    opk::mediaio::TimestampNs timestampNs() const noexcept override {
        return 0;
    }
    opk::MemoryType memoryType() const noexcept override {
        return opk::MemoryType::Host;
    }
    std::span<const opk::mediaio::DataView> planes() const noexcept override {
        return views;
    }
    bool canMap(opk::AccessMode) const noexcept override {
        return mappable;
    }
    std::unique_ptr<opk::mediaio::VideoFrame> map(opk::AccessMode access) const override {
        ++mapCount;
        return mappable && access == opk::AccessMode::Read
                   ? opk::mediaio::makeReadOnlyPixelBufferVideoFrame(
                         pixels.data(), pixels.size(), w, h, pixelFormat, stride)
                   : nullptr;
    }
    uint32_t w, h, stride;
    std::vector<uint8_t> pixels;
    std::vector<opk::mediaio::DataView> views;
    opk::RawImagePixelFormat pixelFormat = opk::RawImagePixelFormat::Bgra;
    bool mappable = false;
    mutable size_t mapCount = 0;
};

class PaddleOcrPreprocess : public ::testing::Test {
  protected:
    void prepare(bool detection, uint32_t w = 4, uint32_t h = 2) {
        inference = std::make_unique<FakeInference>(detection);
        opk::AttributeMap attributes;
        attributes.set("mode", detection ? "detection" : "recognition");
        attributes.set("paddingValue", 0.0);
        ASSERT_TRUE(op.configure(attributes));
        ASSERT_TRUE(op.bind(0, {&op, inference.get()}));
        frame = std::make_shared<TestFrame>(w, h);
        context.videoFrames["pipelineVideoFrame"] = frame;
        context.inferenceImageCrops = {{0, 0, w, h}};
        context.inferenceImageCropIds = {123};
        context.inferenceInfo.inferElementId = "ocr-demo";
    }
    void run() {
        const auto result = op.process(context);
        ASSERT_TRUE(result) << (result ? "" : result.error().toString());
        EXPECT_EQ(*result, opk::op::OpSignal::Continue);
        EXPECT_EQ(inference->values.front(), 42.0f);
        EXPECT_EQ(inference->values.back(), 42.0f);
    }
    opk::stdop::PaddleOcrPreprocessOp op;
    std::unique_ptr<FakeInference> inference;
    std::shared_ptr<TestFrame> frame;
    opk::op::OpChainContext context;
};

TEST_F(PaddleOcrPreprocess, DetectionUsesBgrPlanesAndCenteredNormalizedZeroPadding) {
    prepare(true);
    run();
    const auto &image = context.inferenceInfo.image;
    EXPECT_EQ(image.width, 4U);
    EXPECT_EQ(image.height, 2U);
    EXPECT_EQ(image.modelWidth, 736U);
    EXPECT_EQ(image.modelHeight, 736U);
    EXPECT_EQ(image.letterboxLeft, 0U);
    EXPECT_EQ(image.letterboxRight, 0U);
    EXPECT_EQ(image.letterboxTop, 184U);
    EXPECT_EQ(image.letterboxBottom, 184U);
    const std::array<float, 3> expected{(10 / 255.0f - 0.485f) / 0.229f,
                                        (100 / 255.0f - 0.456f) / 0.224f,
                                        (240 / 255.0f - 0.406f) / 0.225f};
    for (size_t c = 0; c < 3; ++c) {
        EXPECT_FLOAT_EQ(inference->at(c, 0, 183), 0);
        EXPECT_FLOAT_EQ(inference->at(c, 0, 184), expected[c]);
        EXPECT_FLOAT_EQ(inference->at(c, 735, 551), expected[c]);
        EXPECT_FLOAT_EQ(inference->at(c, 735, 552), 0);
    }
}

TEST_F(PaddleOcrPreprocess, DetectionHandlesPortraitSquareAndOddPadding) {
    for (const auto dims : {std::array<uint32_t, 2>{2, 4}, {3, 3}, {3, 2}, {1, 2000}}) {
        prepare(true, dims[0], dims[1]);
        run();
        const auto &image = context.inferenceInfo.image;
        if (dims[0] == 2) {
            EXPECT_EQ(image.letterboxLeft, 184U);
            EXPECT_EQ(image.letterboxRight, 184U);
            EXPECT_EQ(image.letterboxTop, 0U);
        } else if (dims[0] == dims[1]) {
            EXPECT_EQ(image.letterboxLeft + image.letterboxRight + image.letterboxTop +
                          image.letterboxBottom,
                      0U);
        } else if (dims[0] == 3) {
            EXPECT_EQ(image.letterboxTop, 122U);
            EXPECT_EQ(image.letterboxBottom, 123U);
        } else {
            EXPECT_EQ(image.letterboxLeft, 367U);
            EXPECT_EQ(image.letterboxRight, 368U);
        }
    }
}

TEST_F(PaddleOcrPreprocess, RecognitionPreservesAspectAndPadsOnlyOnRight) {
    prepare(false);
    run();
    const auto &image = context.inferenceInfo.image;
    EXPECT_EQ(image.modelHeight, 48U);
    EXPECT_EQ(image.modelWidth, 320U);
    EXPECT_EQ(image.letterboxLeft + image.letterboxTop + image.letterboxBottom, 0U);
    EXPECT_EQ(image.letterboxRight, 224U);
    for (size_t c = 0; c < 3; ++c) {
        const std::array<float, 3> values{10, 100, 240};
        EXPECT_FLOAT_EQ(inference->at(c, 0, 0), values[c] / 127.5f - 1);
        EXPECT_FLOAT_EQ(inference->at(c, 95, 47), values[c] / 127.5f - 1);
        EXPECT_FLOAT_EQ(inference->at(c, 96, 0), 0);
        EXPECT_FLOAT_EQ(inference->at(c, 319, 47), 0);
    }
}

TEST_F(PaddleOcrPreprocess, RecognitionRoundsWidthUpAndCompressesLongLinesWithoutClipping) {
    prepare(false, 1, 5);
    run();
    EXPECT_EQ(context.inferenceInfo.image.letterboxRight, 310U); // ceil(48/5) = 10

    prepare(false, 640, 48);
    for (size_t y = 0; y < 48; ++y) {
        frame->setPixel(638, y, {255, 255, 255, 0});
        frame->setPixel(639, y, {255, 255, 255, 0});
    }
    run();
    EXPECT_EQ(context.inferenceInfo.image.letterboxRight, 0U);
    EXPECT_FLOAT_EQ(inference->at(0, 319, 0), 1.0f);
}

TEST_F(PaddleOcrPreprocess, BilinearResizeUsesHalfPixelCentersAndIgnoresAlpha) {
    prepare(false, 2, 2);
    frame->setPixel(0, 0, {0, 20, 40, 255});
    frame->setPixel(1, 0, {48, 68, 88, 0});
    frame->setPixel(0, 1, {96, 116, 136, 0});
    frame->setPixel(1, 1, {144, 164, 184, 255});
    run();
    // Resizing 2x2 to 48x48: destination (23,23) samples (23/48,23/48).
    // The independent linear ramp is 48*x + 96*y = 69 before normalization.
    EXPECT_FLOAT_EQ(inference->at(0, 23, 23), 69.0f / 127.5f - 1);
    EXPECT_FLOAT_EQ(inference->at(1, 23, 23), 89.0f / 127.5f - 1);
    EXPECT_FLOAT_EQ(inference->at(2, 23, 23), 109.0f / 127.5f - 1);
    EXPECT_FLOAT_EQ(inference->at(0, 0, 0), -1);
    EXPECT_FLOAT_EQ(inference->at(2, 47, 47), 184.0f / 127.5f - 1);
}

TEST_F(PaddleOcrPreprocess, CropsUseOffsetsStrideAndMatchingObjectIdsUntilLoopEnds) {
    prepare(false);
    frame->setPixel(2, 1, {255, 0, 128, 0});
    context.inferenceImageCrops.push_back({2, 1, 1, 1});
    context.inferenceImageCropIds.push_back(456);
    run();
    EXPECT_FLOAT_EQ(inference->at(0, 0, 0), 1);
    EXPECT_FLOAT_EQ(inference->at(1, 47, 47), -1);
    EXPECT_EQ(context.inferenceInfo.parentId, 456U);
    EXPECT_EQ(context.inferenceInfo.image.width, 1U);
    EXPECT_EQ(context.inferenceInfo.image.height, 1U);
    EXPECT_EQ(context.inferenceInfo.modelName, "test-paddle");
    EXPECT_EQ(context.inferenceInfo.contentType, "test-text");
    EXPECT_EQ(context.inferenceInfo.inferElementId, "ocr-demo");
    ASSERT_EQ(context.inferenceImageCrops.size(), 1U);
    ASSERT_EQ(context.inferenceImageCropIds.size(), 1U);
    run();
    EXPECT_EQ(context.inferenceInfo.parentId, 123U);
    EXPECT_FLOAT_EQ(inference->at(0, 319, 47), 0); // Padding is cleared on every crop.
    const auto finished = op.process(context);
    ASSERT_TRUE(finished);
    EXPECT_EQ(*finished, opk::op::OpSignal::BreakLoop);
}

TEST_F(PaddleOcrPreprocess, SupportsNamedSourceSelectedInputAndReadableMapping) {
    prepare(false);
    opk::AttributeMap attributes;
    attributes.set("mode", "recognition");
    attributes.set("paddingValue", 0.0);
    attributes.set("inputImageSourceName", "ocr-input");
    attributes["inputImageTensorIndex"] = 1;
    ASSERT_TRUE(op.configure(attributes));
    inference->model.inputs.insert(inference->model.inputs.begin(), opk::ModelInput{});
    ASSERT_TRUE(op.bind(0, {&op, inference.get()}));
    context.videoFrames.erase("pipelineVideoFrame");
    context.videoFrames["ocr-input"] = frame;
    frame->views.clear();
    frame->mappable = true;
    run();
    EXPECT_EQ(frame->mapCount, 1U);
    EXPECT_EQ(inference->lastInputIndex, 1U);
    EXPECT_FLOAT_EQ(inference->at(0, 0, 0), 10 / 127.5f - 1);
}

TEST_F(PaddleOcrPreprocess, RejectsMissingOrMalformedInputsWithoutConsumingCrops) {
    prepare(false);
    context.inferenceImageCropIds.clear();
    EXPECT_FALSE(op.process(context));
    context.inferenceImageCropIds = {123};
    for (const auto crop : {opk::PixelRect{0, 0, 0, 1},
                            {0, 0, 5, 1},
                            {4, 0, 1, 1},
                            {std::numeric_limits<size_t>::max(), 0, 2, 1}}) {
        context.inferenceImageCrops = {crop};
        EXPECT_FALSE(op.process(context));
        EXPECT_EQ(context.inferenceImageCrops.size(), 1U);
        EXPECT_EQ(context.inferenceImageCropIds.size(), 1U);
    }
    context.inferenceImageCrops = {{0, 0, 4, 2}};
    context.videoFrames.clear();
    EXPECT_FALSE(op.process(context));
    context.videoFrames["pipelineVideoFrame"] = frame;
    frame->pixelFormat = opk::RawImagePixelFormat::Rgb;
    EXPECT_FALSE(op.process(context));
    frame->pixelFormat = opk::RawImagePixelFormat::Bgra;
    frame->views[0] =
        opk::mediaio::DataView::host(frame->pixels.data(), 1, frame->stride, opk::AccessMode::Read);
    EXPECT_FALSE(op.process(context));
    frame->views[0] = opk::mediaio::DataView::host(
        frame->pixels.data(), frame->pixels.size(), 3, opk::AccessMode::Read);
    EXPECT_FALSE(op.process(context));
    frame->views.clear();
    EXPECT_FALSE(op.process(context)); // Mapping is unavailable.
    frame->views.push_back(opk::mediaio::DataView::host(
        frame->pixels.data(), frame->pixels.size(), frame->stride, opk::AccessMode::Write));
    EXPECT_FALSE(op.process(context));
    EXPECT_EQ(context.inferenceImageCrops.size(), 1U);
    EXPECT_EQ(context.inferenceImageCropIds.size(), 1U);
}

TEST_F(PaddleOcrPreprocess, RejectsInvalidConfigurationAndBinding) {
    FakeInference backend;
    EXPECT_FALSE(op.bind(0, {&op, &backend}));
    EXPECT_FALSE(op.process(context));
    EXPECT_FALSE(op.configure({}));
    opk::AttributeMap attributes;
    attributes.set("paddingValue", 0.0);
    for (const auto &bad : {opk::AttributeValue("unknown"), opk::AttributeValue(1)}) {
        attributes["mode"] = bad;
        EXPECT_FALSE(op.configure(attributes));
    }
    attributes.set("mode", "recognition");
    for (const auto &bad :
         {opk::AttributeValue(-1), opk::AttributeValue(16), opk::AttributeValue("0")}) {
        attributes["inputImageTensorIndex"] = bad;
        EXPECT_FALSE(op.configure(attributes));
    }
    attributes.raw().erase("inputImageTensorIndex");
    for (const auto *bad : {"", " leading", "trailing "}) {
        attributes.set("inputImageSourceName", bad);
        EXPECT_FALSE(op.configure(attributes));
    }
    attributes.raw().erase("inputImageSourceName");
    attributes.set("unknown", true);
    EXPECT_FALSE(op.configure(attributes));
    attributes.raw().erase("unknown");
    ASSERT_TRUE(op.configure(attributes));
    EXPECT_FALSE(op.bind(0, {}));
    EXPECT_FALSE(op.bind(0, {&op}));
    EXPECT_FALSE(op.bind(std::numeric_limits<size_t>::max(), {&op}));
    EXPECT_FALSE(op.bind(0, {&op, nullptr}));
    EXPECT_FALSE(op.bind(0, {&op, &op}));
    backend.model.inputs[0].shape = opk::Shape(1, 1, 48, 320);
    EXPECT_FALSE(op.bind(0, {&op, &backend}));
    backend.model.inputs[0].shape = opk::Shape(1, 3, 48, 320);
    backend.model.inputs[0].valueType = opk::Dtype::Uint8;
    EXPECT_FALSE(op.bind(0, {&op, &backend}));
    backend.model.inputs[0].valueType = opk::Dtype::Float32;
    backend.model.inputs[0].dataKind = opk::DataKind::ImageRgbChw;
    EXPECT_FALSE(op.bind(0, {&op, &backend}));
    backend.model.inputs[0].dataKind = opk::DataKind::ImageBgrChw;
    backend.missingBuffer = true;
    EXPECT_FALSE(op.bind(0, {&op, &backend}));
    backend.missingBuffer = false;
    ASSERT_TRUE(op.bind(0, {&op, &backend}));
    backend.missingBuffer = true;
    context.inferenceImageCrops = {{0, 0, 1, 1}};
    context.inferenceImageCropIds = {1};
    context.videoFrames["pipelineVideoFrame"] = std::make_shared<TestFrame>(1, 1);
    EXPECT_FALSE(op.process(context));
}

} // namespace
