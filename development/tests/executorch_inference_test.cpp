/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "Inference.h"
#include "pek/ModelDescriptor.h"
#include "pek/Types.h"

TEST(ExecuTorchInference, RejectsTensorRanksOutsidePekShapeCapacity) {
    const std::vector<int> emptyShape;
    EXPECT_FALSE(pek::extrch::detail::toPekShape(emptyShape).has_value());

    const std::vector<int> maximumRankShape(pek::Shape::MaxRank, 1);
    const auto maximumRankResult = pek::extrch::detail::toPekShape(maximumRankShape);
    ASSERT_TRUE(maximumRankResult.has_value());
    EXPECT_EQ(maximumRankResult->rank, pek::Shape::MaxRank);
    EXPECT_EQ(maximumRankResult->dims[pek::Shape::MaxRank - 1], 1);

    const std::vector<int> excessiveRankShape(pek::Shape::MaxRank + 1, 1);
    EXPECT_FALSE(pek::extrch::detail::toPekShape(excessiveRankShape).has_value());
}

TEST(ExecuTorchInference, LoadsAndRunsPinnedYoloXModel) {
    const char *modelPath = std::getenv("PEK_TEST_EXECUTORCH_MODEL");
    ASSERT_NE(modelPath, nullptr);

    const std::string descriptorJson =
        R"({"name":"yolox","modelFamily":"yolox-od","modelFile":")" + std::string(modelPath) +
        R"(","dynamicOutput":true,"inputTensors":[{"shape":[1,3,416,416],)"
        R"("dataKind":"ImageRgbChw","valueType":"Float32",)"
        R"("std":[0.0039215686,0.0039215686,0.0039215686]}]})";
    const auto descriptor = pek::ModelDescriptor::fromJson(descriptorJson);
    ASSERT_TRUE(descriptor.has_value()) << descriptor.error().toString();

    pek::extrch::Inference inference;
    const auto setupResult = inference.setup(*descriptor);
    ASSERT_TRUE(setupResult.has_value()) << setupResult.error().toString();
    ASSERT_TRUE(inference.isReady());

    const pek::Model &model = inference.getModel();
    ASSERT_EQ(model.inputs.size(), 1U);
    ASSERT_FALSE(model.outputs.empty());

    const size_t inputBytes = model.inputs.front().shape.getFullValueCount() *
                              pek::getValueTypeByteSize(model.inputs.front().valueType);
    std::fill_n(inference.getInputTensorDataAddress(0), inputBytes, uint8_t{0});

    const auto inferenceResult = inference.inference();
    ASSERT_TRUE(inferenceResult.has_value()) << inferenceResult.error().toString();

    for (size_t i = 0; i < model.outputs.size(); ++i) {
        EXPECT_NE(inference.getOutputTensorDataAddress(i), nullptr);
        const pek::Shape outputShape = inference.getOutputTensorFinalShape(i);
        EXPECT_GT(outputShape.rank, 0);
        EXPECT_GT(outputShape.getFullValueCount(), 0U);
    }
}
