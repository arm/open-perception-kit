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

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "opk/AttributeMap.h"
#include "opk/FrameResults.h"
#include "opk/TensorParser.h"
#include "opk/TensorView.h"
#include "postproc/YoloParser.h"

TEST(YoloParser, RejectsInvalidCategoryIdsBeforePublishingUnsignedIdentity) {
    opk::AttributeMap attrs;
    attrs.set("outputFormat", "cornerScoreClass");
    attrs.set("applyNms", false);
    std::vector<float> values;
    for (const float id : {0.0f,
                           -1.0f,
                           .5f,
                           std::numeric_limits<float>::quiet_NaN(),
                           std::numeric_limits<float>::infinity(),
                           std::numeric_limits<float>::max()})
        values.insert(values.end(), {10, 20, 30, 40, .9f, id});
    opk::TensorView tensor(values.data(),
                           values.size() * sizeof(float),
                           opk::Shape(1, 6, 6),
                           opk::Dtype::Float32,
                           1,
                           0);
    opk::TensorParser::Input input(attrs);
    input.tensors[0] = &tensor;
    input.inferenceInfo.image = {.width = 100,
                                 .height = 100,
                                 .modelWidth = 100,
                                 .modelHeight = 100,
                                 .frameId = 1,
                                 .frameWidth = 100,
                                 .frameHeight = 100};
    open_perception_kit::FrameResults results;
    opk::stdop::postproc::YoloParser parser;
    ASSERT_TRUE(parser.parse(input, results));
    results.for_each<open_perception_kit::metadata::BoxDetectionsT>([](const auto &payload) {
        ASSERT_EQ(payload.detections.size(), 1U);
        EXPECT_EQ(payload.detections[0]->class_id, 0U);
    });
}

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

TEST(YoloParser, SuccessfulEmptyDetectionIsStillAPublishedResult) {
    opk::AttributeMap attributes;
    std::vector<float> values(std::size_t{6} * 8, 0);
    opk::TensorView tensor(values.data(),
                           values.size() * sizeof(float),
                           opk::Shape(1, 6, 8),
                           opk::Dtype::Float32,
                           1,
                           0);
    opk::TensorParser::Input input(attributes);
    input.tensors[0] = &tensor;
    input.inferenceInfo.image = {
        .width = 100, .height = 100, .modelWidth = 100, .modelHeight = 100};
    input.inferenceInfo.modelName = "empty-yolo";
    open_perception_kit::FrameResults result;
    ASSERT_TRUE(opk::stdop::postproc::YoloParser().parse(input, result));
    ASSERT_EQ(result.count<open_perception_kit::metadata::BoxDetectionsT>(), 1U);
    result.for_each<open_perception_kit::metadata::BoxDetectionsT>([](const auto &payload) {
        EXPECT_TRUE(payload.detections.empty());
        EXPECT_EQ(payload.layer->model, "empty-yolo");
    });
}

TEST(YoloParser, ZeroThresholdPreservesZeroScoreAndSelectsARealCategory) {
    for (const bool channelsFirst : {true, false}) {
        opk::AttributeMap attrs;
        attrs.set("outputFormat", "centerClassScores");
        attrs.set("confidenceThreshold", 0.0);
        attrs.set("applyNms", false);
        constexpr std::size_t Channels = 6;
        constexpr std::size_t Candidates = 8;
        std::vector<float> values(Channels * Candidates, 0);
        const auto index = [channelsFirst](const std::size_t row, const std::size_t channel) {
            return channelsFirst ? channel * Candidates + row : row * Channels + channel;
        };
        for (std::size_t row = 0; row < Candidates; ++row) {
            values[index(row, 0)] = 50;
            values[index(row, 1)] = 50;
            values[index(row, 2)] = 20;
            values[index(row, 3)] = 10;
        }
        opk::TensorView tensor(values.data(),
                               values.size() * sizeof(float),
                               channelsFirst ? opk::Shape(1, 6, 8) : opk::Shape(1, 8, 6),
                               opk::Dtype::Float32,
                               1,
                               0);
        opk::TensorParser::Input input(attrs);
        input.tensors[0] = &tensor;
        input.inferenceInfo.image = {.width = 100,
                                     .height = 100,
                                     .modelWidth = 100,
                                     .modelHeight = 100,
                                     .frameId = 1,
                                     .frameWidth = 100,
                                     .frameHeight = 100};
        opk::stdop::postproc::YoloParser parser;
        open_perception_kit::FrameResults results;
        ASSERT_TRUE(parser.parse(input, results));
        EXPECT_EQ(results.count<open_perception_kit::metadata::BoxDetectionsT>(), 1U);
        results.for_each<open_perception_kit::metadata::BoxDetectionsT>(
            [expected = Candidates](const auto &payload) {
                ASSERT_EQ(payload.detections.size(), expected);
                for (const auto &detection : payload.detections) {
                    EXPECT_EQ(detection->class_id, 0U);
                    EXPECT_FLOAT_EQ(detection->confidence, 0);
                }
            });
        values[index(0, 5)] = std::numeric_limits<float>::quiet_NaN();
        open_perception_kit::FrameResults invalid;
        EXPECT_FALSE(parser.parse(input, invalid));
        EXPECT_EQ(invalid.count<open_perception_kit::metadata::BoxDetectionsT>(), 0U);
    }
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

TEST(YoloParser, SuppressesOverlappingBoxesAfterRestoringFrameCoordinates) {
    opk::AttributeMap attrs;
    attrs.set("outputFormat", "cornerScoreClass");
    attrs.set("coordinatesAreNormalized", true);
    attrs.set("iouThreshold", 0.8);

    std::vector<float> data{-.1f, .2f, .3f, .4f, .9f, 2, 0, .2f, .3f, .4f, .8f, 2};
    opk::TensorView tensor(data.data(),
                           data.size() * sizeof(float),
                           opk::Shape(1, 2, 6),
                           opk::Dtype::Float32,
                           1.0f,
                           0.0f);
    opk::TensorParser::Input input(attrs);
    input.tensors[0] = &tensor;
    input.inferenceInfo.image = {.width = 200,
                                 .height = 160,
                                 .modelWidth = 100,
                                 .modelHeight = 100,
                                 .letterboxTop = 10,
                                 .letterboxBottom = 10,
                                 .frameId = 42,
                                 .frameWidth = 640,
                                 .frameHeight = 480,
                                 .cropX = 100,
                                 .cropY = 50};

    open_perception_kit::FrameResults output;
    opk::stdop::postproc::YoloParser parser;
    const auto result = parser.parse(input, output);
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    const auto payload = output.get<open_perception_kit::metadata::BoxDetectionsT>();
    if (!payload.has_value()) {
        ADD_FAILURE() << "missing BoxDetections payload";
        return;
    }
    const auto &detections = payload.value().value().detections;
    ASSERT_EQ(detections.size(), 1U);
    EXPECT_EQ(detections[0]->class_id, 2);
    EXPECT_FLOAT_EQ(detections[0]->confidence, .9f);
    EXPECT_FLOAT_EQ(detections[0]->box->x, 0);
    EXPECT_FLOAT_EQ(detections[0]->box->y, .125f);
    EXPECT_FLOAT_EQ(detections[0]->box->width, .3f);
    EXPECT_FLOAT_EQ(detections[0]->box->height, .25f);
    const auto projected = open_perception_kit::projectBoundingBox(*detections[0]->box, 1280, 960);
    if (!projected) {
        ADD_FAILURE() << "missing projected detection";
        return;
    }
    EXPECT_NEAR(projected.value()[0], 200, 1e-4);
    EXPECT_NEAR(projected.value()[1], 140, 1e-4);
    EXPECT_NEAR(projected.value()[2], 120, 1e-4);
    EXPECT_NEAR(projected.value()[3], 80, 1e-4);
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

TEST(YoloParser, CornerClassScoresKeepsTwoStageTopKTiesAndLetterboxGeometry) {
    opk::AttributeMap attrs;
    attrs.set("outputFormat", "cornerClassScores");
    attrs.set("maxDetections", std::int64_t{2});
    attrs.set("normalizeOutputCoordinates", false);
    attrs.set("applyNms", false);
    std::vector<float> data{50, 50, 60, 60, .8f, .1f, 10, 20, 30, 40, .9f, .8f};
    opk::TensorView tensor(data.data(),
                           data.size() * sizeof(float),
                           opk::Shape(1, 2, 6),
                           opk::Dtype::Float32,
                           1.0f,
                           0.0f);
    opk::TensorParser::Input input(attrs);
    input.tensors[0] = &tensor;
    input.inferenceInfo.image = {.width = 100,
                                 .height = 80,
                                 .modelWidth = 100,
                                 .modelHeight = 100,
                                 .letterboxTop = 10,
                                 .letterboxBottom = 10};
    open_perception_kit::FrameResults output;
    opk::stdop::postproc::YoloParser parser;
    ASSERT_TRUE(parser.parse(input, output).has_value());
    const auto payload = output.get<open_perception_kit::metadata::BoxDetectionsT>();
    if (!payload.has_value()) {
        ADD_FAILURE() << "missing BoxDetections payload";
        return;
    }
    const auto &detections = payload.value().value().detections;
    ASSERT_EQ(detections.size(), 2U);
    EXPECT_EQ(detections[0]->class_id, 0);
    EXPECT_EQ(detections[1]->class_id, 1);
    EXPECT_FLOAT_EQ(detections[1]->confidence, .8f);
    EXPECT_FLOAT_EQ(detections[1]->box->x, 10);
    EXPECT_FLOAT_EQ(detections[1]->box->y, 10);
    EXPECT_FLOAT_EQ(detections[1]->box->height, 20);
    attrs.set("maxDetections", std::int64_t{0});
    EXPECT_FALSE(parser.parse(input, output).has_value());
}

TEST(YoloParser, CornerClassScoresRejectsMalformedAndNonFiniteValues) {
    opk::AttributeMap attrs;
    attrs.set("outputFormat", "cornerClassScores");
    std::vector<float> data{10, 20, 30, 40, .9f};
    opk::TensorView tensor(data.data(),
                           data.size() * sizeof(float),
                           opk::Shape(1, 1, 5),
                           opk::Dtype::Float32,
                           1.0f,
                           0.0f);
    opk::TensorView shortTensor(
        data.data(), 4 * sizeof(float), opk::Shape(1, 1, 4), opk::Dtype::Float32, 1.0f, 0.0f);
    opk::TensorParser::Input input(attrs);
    input.inferenceInfo.image = {
        .width = 100, .height = 100, .modelWidth = 100, .modelHeight = 100};
    open_perception_kit::FrameResults output;
    opk::stdop::postproc::YoloParser parser;
    input.tensors[0] = &shortTensor;
    EXPECT_FALSE(parser.parse(input, output).has_value());
    input.tensors[0] = &tensor;
    data[4] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(parser.parse(input, output).has_value());
    data[4] = .9f;
    data[0] = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(parser.parse(input, output).has_value());
    EXPECT_EQ(output.count<open_perception_kit::metadata::BoxDetectionsT>(), 0U);
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
