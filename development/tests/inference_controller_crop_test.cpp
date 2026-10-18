/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

#include "InferenceControllerOp.h"
#include "mediaio/PixelBufferVideoFrame.h"
#include "op/OpChainContext.h"
#include "opk/AttributeMap.h"
#include "opk/FrameResults.h"
#include "opk/TensorParser.h"
#include "opk/TensorView.h"
#include "postproc/UltrafaceParser.h"
#include "postproc/YoloParser.h"

namespace {

constexpr size_t FrameWidth = 640;
constexpr size_t FrameHeight = 480;

std::vector<opk::PixelRect> selectCrops(opk::stdop::InferenceControllerOp &controller,
                                        open_perception_kit::FrameResults &results) {
    std::vector<uint8_t> pixels(FrameWidth * FrameHeight * 4, 0x7f);
    auto frame = opk::mediaio::PixelBufferVideoFrame::borrowReadOnly(pixels.data(),
                                                                     pixels.size(),
                                                                     FrameWidth,
                                                                     FrameHeight,
                                                                     opk::RawImagePixelFormat::Bgra,
                                                                     FrameWidth * 4);
    if (!frame) {
        ADD_FAILURE() << "failed to create source frame";
        return {};
    }

    opk::op::OpChainContext context;
    context.frameResults = &results;
    context.videoFrames["pipelineVideoFrame"] =
        std::shared_ptr<opk::mediaio::VideoFrame>(std::move(frame));
    const auto processed = controller.process(context);
    EXPECT_TRUE(processed.has_value()) << processed.error().toString();
    return context.inferenceImageCrops;
}

std::vector<opk::PixelRect> ultrafaceCrops(opk::stdop::InferenceControllerOp &controller,
                                           float firstBoxCoordinate) {
    constexpr size_t DetectionCount = 4420;
    std::vector<float> scores(DetectionCount * 2, 0.0f);
    std::vector<float> boxes(DetectionCount * 4, 0.0f);
    scores[1] = 1.0f;
    boxes[0] = firstBoxCoordinate;

    opk::TensorView scoreView(scores.data(),
                              scores.size() * sizeof(float),
                              opk::Shape(1, 4420, 2),
                              opk::Dtype::Float32,
                              1,
                              0);
    opk::TensorView boxView(boxes.data(),
                            boxes.size() * sizeof(float),
                            opk::Shape(1, 4420, 4),
                            opk::Dtype::Float32,
                            1,
                            0);
    opk::AttributeMap attributes;
    attributes.set("normalizeOutputCoordinates", false);
    attributes.set("confidenceThreshold", 0.5);
    attributes.set("iouThreshold", 0.3);
    opk::TensorParser::Input input(attributes);
    input.tensors[0] = &scoreView;
    input.tensors[1] = &boxView;
    input.inferenceInfo.image = {
        .width = FrameWidth, .height = FrameHeight, .modelWidth = 320, .modelHeight = 240};

    open_perception_kit::FrameResults results;
    opk::stdop::postproc::UltraFaceParser parser;
    const auto parsed = parser.parse(input, results);
    EXPECT_TRUE(parsed.has_value()) << parsed.error().toString();
    return selectCrops(controller, results);
}

std::vector<opk::PixelRect> yoloCrops(opk::stdop::InferenceControllerOp &controller, float x2) {
    std::vector<float> output{10.0f, 20.0f, x2, 40.0f, 0.9f, 2.0f};
    opk::TensorView view(output.data(),
                         output.size() * sizeof(float),
                         opk::Shape(1, 1, 6),
                         opk::Dtype::Float32,
                         1,
                         0);
    opk::AttributeMap attributes;
    attributes.set("outputFormat", "cornerScoreClass");
    attributes.set("normalizeOutputCoordinates", false);
    attributes.set("applyNms", false);
    attributes.set("confidenceThreshold", 0.4);
    opk::TensorParser::Input input(attributes);
    input.tensors[0] = &view;
    input.inferenceInfo.image = {
        .width = FrameWidth, .height = FrameHeight, .modelWidth = 320, .modelHeight = 320};

    open_perception_kit::FrameResults results;
    opk::stdop::postproc::YoloParser parser;
    const auto parsed = parser.parse(input, results);
    EXPECT_TRUE(parsed.has_value()) << parsed.error().toString();
    return selectCrops(controller, results);
}

void expectValidCrop(const std::vector<opk::PixelRect> &crops) {
    ASSERT_EQ(crops.size(), 1U);
    EXPECT_FALSE(crops.front().isEmpty());
    EXPECT_TRUE(crops.front().fitsWithin(FrameWidth, FrameHeight));
}

} // namespace

TEST(InferenceControllerCrop, RejectsNonFiniteUltrafaceBoxAndRecovers) {
    opk::stdop::InferenceControllerOp controller;
    opk::AttributeMap attributes;
    attributes.set("contentType", "humanFace");
    ASSERT_TRUE(controller.configure(attributes).has_value());

    expectValidCrop(ultrafaceCrops(controller, 0.0f));
    EXPECT_TRUE(ultrafaceCrops(controller, std::nanf("test")).empty());
    expectValidCrop(ultrafaceCrops(controller, 0.0f));
}

TEST(InferenceControllerCrop, RejectsInvertedYoloBoxAndRecovers) {
    opk::stdop::InferenceControllerOp controller;
    opk::AttributeMap attributes;
    attributes.set("contentType", "genericObject");
    ASSERT_TRUE(controller.configure(attributes).has_value());

    expectValidCrop(yoloCrops(controller, 30.0f));
    EXPECT_TRUE(yoloCrops(controller, 5.0f).empty());
    expectValidCrop(yoloCrops(controller, 30.0f));
}
