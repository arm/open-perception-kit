/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "pek/AttributeMap.h"
#include "pek/FrameResults.h"
#include "pek/TensorParser.h"
#include "pek/TensorView.h"
#include "postproc/YoloParser.h"

TEST(YoloParser, StoresBestClassIdOnRectOutput) {
    pek::AttributeMap attrs;
    attrs.set("outputFormat", "UltralyticsYolo");
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

    pek::TensorView tensor(tensorData.data(),
                           tensorData.size() * sizeof(float),
                           pek::Shape(1, 7, 8),
                           pek::Dtype::Float32,
                           1.0f,
                           0.0f);

    pek::TensorParser::Input input(attrs);
    input.tensors[0] = &tensor;
    input.inferenceInfo.image.width = 100;
    input.inferenceInfo.image.height = 100;
    input.inferenceInfo.image.modelWidth = 100;
    input.inferenceInfo.image.modelHeight = 100;

    perception::FrameResults output;
    pek::stdop::postproc::YoloParser parser;

    const auto result = parser.parse(input, output);

    ASSERT_TRUE(result.has_value()) << result.error().toString();
    ASSERT_EQ(output.count<perception::metadata::BoxDetectionsT>(), 1U);

    const auto detectionsRef = output.get<perception::metadata::BoxDetectionsT>();
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

TEST(YoloParser, ParsesPackedHailoOutput) {
    pek::AttributeMap attrs;
    attrs.set("outputFormat", "HailoYoloNMS");
    attrs.set("normalizeOutputCoordinates", false);
    attrs.set("applyNms", false);
    attrs.set("classCount", std::int64_t{2});
    attrs.set("maxBboxesPerClass", std::int64_t{1});

    std::vector<float> tensorData{0.0f, 1.0f, 10.0f, 20.0f, 30.0f, 50.0f, 0.9f, 0.0f, 0.0f, 0.0f};
    pek::TensorView tensor(tensorData.data(),
                           tensorData.size() * sizeof(float),
                           pek::Shape(1, 2, 5),
                           pek::Dtype::Float32,
                           1.0f,
                           0.0f);

    pek::TensorParser::Input input(attrs);
    input.tensors[0] = &tensor;
    input.inferenceInfo.image.width = 100;
    input.inferenceInfo.image.height = 100;
    input.inferenceInfo.image.modelWidth = 100;
    input.inferenceInfo.image.modelHeight = 100;

    perception::FrameResults output;
    pek::stdop::postproc::YoloParser parser;
    const auto result = parser.parse(input, output);

    ASSERT_TRUE(result.has_value()) << result.error().toString();
    ASSERT_EQ(output.count<perception::metadata::BoxDetectionsT>(), 1U);

    const auto detectionsRef = output.get<perception::metadata::BoxDetectionsT>();
    ASSERT_TRUE(detectionsRef.has_value());
    const auto &detections = detectionsRef.value().value().detections;
    ASSERT_EQ(detections.size(), 1U);
    ASSERT_NE(detections[0], nullptr);
    ASSERT_NE(detections[0]->box, nullptr);
    EXPECT_EQ(detections[0]->class_id, 1);
    EXPECT_FLOAT_EQ(detections[0]->box->x, 20.0f);
    EXPECT_FLOAT_EQ(detections[0]->box->y, 10.0f);
    EXPECT_FLOAT_EQ(detections[0]->box->width, 30.0f);
    EXPECT_FLOAT_EQ(detections[0]->box->height, 20.0f);
    EXPECT_FLOAT_EQ(detections[0]->confidence, 0.9f);
}
