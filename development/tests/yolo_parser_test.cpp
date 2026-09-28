/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 *************************************************************/

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "opk/AttributeMap.h"
#include "opk/FrameResults.h"
#include "opk/TensorParser.h"
#include "opk/TensorView.h"
#include "postproc/YoloParser.h"

TEST(YoloParser, StoresBestClassIdOnRectOutput) {
    opk::AttributeMap attrs;
    attrs.set("normalizeOutputCoordinates", false);
    attrs.set("applyNms", false);

    constexpr std::size_t rows = 7;
    constexpr std::size_t cols = 8;

    std::vector<float> tensorData(rows * cols, 0.0f);
    tensorData[0U * cols] = 50.0f; // cx
    tensorData[1U * cols] = 50.0f; // cy
    tensorData[2U * cols] = 20.0f; // width
    tensorData[3U * cols] = 10.0f; // height
    tensorData[4U * cols] = 0.1f;  // class 0
    tensorData[5U * cols] = 0.9f;  // class 1
    tensorData[6U * cols] = 0.2f;  // class 2

    opk::TensorView tensor(tensorData.data(),
                           tensorData.size() * sizeof(float),
                           opk::Shape(1, 7, 8),
                           opk::Dtype::Float32,
                           1.0f,
                           0.0f);

    opk::TensorParser::Input input(attrs);
    input.tensors[0] = &tensor;
    input.inferenceInfo.image.width = 100;
    input.inferenceInfo.image.height = 100;
    input.inferenceInfo.image.modelWidth = 100;
    input.inferenceInfo.image.modelHeight = 100;

    open_perception_kit::FrameResults output;
    opk::stdop::postproc::YoloParser parser;

    const auto result = parser.parse(input, output);

    ASSERT_TRUE(result.has_value()) << result.error().toString();
    ASSERT_EQ(output.count<open_perception_kit::metadata::BoxDetectionsT>(), 1U);

    const auto detectionsRef = output.get<open_perception_kit::metadata::BoxDetectionsT>();
    if (!detectionsRef.has_value()) {
        ADD_FAILURE() << "missing BoxDetections payload";
        return;
    }
    const auto &detections = detectionsRef.value().value().detections;
    ASSERT_EQ(detections.size(), 1U);
    ASSERT_NE(detections[0], nullptr);
    EXPECT_EQ(detections[0]->class_id, 1);
    EXPECT_FLOAT_EQ(detections[0]->confidence, 0.9f);
}

TEST(YoloParser, ParsesCornerScoreClassOutput) {
    opk::AttributeMap attrs;
    attrs.set("outputFormat", "cornerScoreClass");
    attrs.set("normalizeOutputCoordinates", false);
    attrs.set("applyNms", false);

    std::vector<float> tensorData{
        10.0f,
        20.0f,
        30.0f,
        40.0f,
        0.9f,
        2.0f,
        50.0f,
        50.0f,
        60.0f,
        60.0f,
        0.1f,
        3.0f,
    };
    opk::TensorView tensor(tensorData.data(),
                           tensorData.size() * sizeof(float),
                           opk::Shape(1, 2, 6),
                           opk::Dtype::Float32,
                           1.0f,
                           0.0f);

    opk::TensorParser::Input input(attrs);
    input.tensors[0] = &tensor;
    input.inferenceInfo.image = {
        .width = 100, .height = 100, .modelWidth = 100, .modelHeight = 100};

    open_perception_kit::FrameResults output;
    opk::stdop::postproc::YoloParser parser;
    const auto result = parser.parse(input, output);

    ASSERT_TRUE(result.has_value()) << result.error().toString();
    const auto detectionsRef = output.get<open_perception_kit::metadata::BoxDetectionsT>();
    if (!detectionsRef.has_value()) {
        ADD_FAILURE() << "missing BoxDetections payload";
        return;
    }
    const auto &detections = detectionsRef.value().value().detections;
    ASSERT_EQ(detections.size(), 1U);
    EXPECT_EQ(detections[0]->class_id, 2);
    EXPECT_FLOAT_EQ(detections[0]->confidence, 0.9f);
    EXPECT_FLOAT_EQ(detections[0]->box->x, 10.0f);
    EXPECT_FLOAT_EQ(detections[0]->box->y, 20.0f);
    EXPECT_FLOAT_EQ(detections[0]->box->width, 20.0f);
    EXPECT_FLOAT_EQ(detections[0]->box->height, 20.0f);
}

TEST(YoloParser, RejectsMissingTensor) {
    opk::AttributeMap attrs;
    opk::TensorParser::Input input(attrs);
    input.inferenceInfo.image = {
        .width = 100, .height = 100, .modelWidth = 100, .modelHeight = 100};

    open_perception_kit::FrameResults output;
    opk::stdop::postproc::YoloParser parser;

    EXPECT_FALSE(parser.parse(input, output).has_value());
}

TEST(YoloParser, RejectsMalformedTensorShape) {
    opk::AttributeMap attrs;
    std::vector<float> tensorData(4, 0.0f);
    opk::TensorView tensor(tensorData.data(),
                           tensorData.size() * sizeof(float),
                           opk::Shape(1, 4),
                           opk::Dtype::Float32,
                           1.0f,
                           0.0f);
    opk::TensorParser::Input input(attrs);
    input.tensors[0] = &tensor;
    input.inferenceInfo.image = {
        .width = 100, .height = 100, .modelWidth = 100, .modelHeight = 100};

    open_perception_kit::FrameResults output;
    opk::stdop::postproc::YoloParser parser;

    EXPECT_FALSE(parser.parse(input, output).has_value());
}

TEST(YoloParser, RejectsInvalidImageSize) {
    opk::AttributeMap attrs;
    opk::TensorParser::Input input(attrs);
    input.inferenceInfo.image = {.width = 100, .height = 100, .modelWidth = 100, .modelHeight = 0};

    open_perception_kit::FrameResults output;
    opk::stdop::postproc::YoloParser parser;

    EXPECT_FALSE(parser.parse(input, output).has_value());
}

TEST(YoloParser, RejectsTensorWithTooFewValuesPerCandidate) {
    opk::AttributeMap attrs;
    std::vector<float> tensorData(32U, 0.0f);
    opk::TensorView tensor(tensorData.data(),
                           tensorData.size() * sizeof(float),
                           opk::Shape(1, 4, 8),
                           opk::Dtype::Float32,
                           1.0f,
                           0.0f);
    opk::TensorParser::Input input(attrs);
    input.tensors[0] = &tensor;
    input.inferenceInfo.image = {
        .width = 100, .height = 100, .modelWidth = 100, .modelHeight = 100};

    open_perception_kit::FrameResults output;
    opk::stdop::postproc::YoloParser parser;

    EXPECT_FALSE(parser.parse(input, output).has_value());
}

TEST(YoloParser, RejectsInvalidTensorView) {
    opk::AttributeMap attrs;
    opk::TensorView tensor(nullptr, 0U, opk::Shape(1, 5, 5), opk::Dtype::Float32, 1.0f, 0.0f);
    opk::TensorParser::Input input(attrs);
    input.tensors[0] = &tensor;
    input.inferenceInfo.image = {
        .width = 100, .height = 100, .modelWidth = 100, .modelHeight = 100};

    open_perception_kit::FrameResults output;
    opk::stdop::postproc::YoloParser parser;

    EXPECT_FALSE(parser.parse(input, output).has_value());
}
