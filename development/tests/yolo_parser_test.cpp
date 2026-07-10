/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include <variant>
#include <vector>

#include "pek/AttributeMap.h"
#include "pek/Perception.h"
#include "pek/TensorParser.h"
#include "pek/TensorView.h"
#include "postproc/YoloParser.h"

TEST(YoloParser, StoresBestClassIdOnRectOutput) {
    pek::AttributeMap attrs;
    attrs.set("normalizeOutputCoordinates", false);
    attrs.set("applyNms", false);

    std::vector<float> tensorData(7 * 8, 0.0f);
    tensorData[0 * 8] = 50.0f; // cx
    tensorData[1 * 8] = 50.0f; // cy
    tensorData[2 * 8] = 20.0f; // width
    tensorData[3 * 8] = 10.0f; // height
    tensorData[4 * 8] = 0.1f;  // class 0
    tensorData[5 * 8] = 0.9f;  // class 1
    tensorData[6 * 8] = 0.2f;  // class 2

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

    pek::Perception::Layer output;
    pek::stdop::postproc::YoloParser parser;

    const auto result = parser.parse(input, output);

    ASSERT_TRUE(result.has_value()) << result.error().toString();
    ASSERT_EQ(output.detections.size(), 1U);

    const auto *rect = std::get_if<pek::Perception::Rect>(&output.detections[0]);
    ASSERT_NE(rect, nullptr);
    EXPECT_EQ(rect->classId, 1);
    EXPECT_FLOAT_EQ(rect->confidence, 0.9f);
}
