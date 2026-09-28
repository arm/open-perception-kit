/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 *************************************************************/

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

#include "opk/AttributeMap.h"
#include "opk/FrameResults.h"
#include "opk/TensorParser.h"
#include "opk/TensorView.h"
#include "postproc/GazeDetectionParser.h"

namespace {

float parseAngleForBin(std::size_t bin, const opk::AttributeMap &attributes) {
    std::vector<float> logits(90U, -100.0f);
    logits[bin] = 100.0f;
    opk::TensorView yaw(logits.data(),
                        logits.size() * sizeof(float),
                        opk::Shape(1, 90),
                        opk::Dtype::Float32,
                        1.0f,
                        0.0f);
    opk::TensorView pitch = yaw;
    opk::TensorParser::Input input(attributes);
    input.tensors[0] = &yaw;
    input.tensors[1] = &pitch;

    open_perception_kit::FrameResults output;
    opk::stdop::postproc::GazeDetectionParser parser;
    const auto result = parser.parse(input, output);
    if (!result) {
        ADD_FAILURE() << result.error().toString();
        return std::numeric_limits<float>::quiet_NaN();
    }

    const auto posesRef = output.get<open_perception_kit::metadata::PoseEstimationsT>();
    if (!posesRef) {
        ADD_FAILURE() << "missing PoseEstimations payload";
        return std::numeric_limits<float>::quiet_NaN();
    }
    const auto &poses = posesRef.value().value().poses;
    if (poses.size() != 1U || !poses[0]) {
        ADD_FAILURE() << "expected one pose";
        return std::numeric_limits<float>::quiet_NaN();
    }
    return poses[0]->yaw;
}

} // namespace

TEST(GazeDetectionParser, UsesConfiguredAngleBinWidth) {
    opk::AttributeMap attributes;
    attributes.set("angleBinWidthDeg", 4.0);

    constexpr std::array<std::pair<std::size_t, float>, 3> Cases = {
        {{0U, -180.0f}, {45U, 0.0f}, {89U, 176.0f}}};
    for (const auto &[bin, expectedAngle] : Cases)
        EXPECT_NEAR(parseAngleForBin(bin, attributes), expectedAngle, 0.001f);
}

TEST(GazeDetectionParser, PreservesLegacyMappingWithoutAngleBinWidth) {
    const opk::AttributeMap attributes;
    EXPECT_NEAR(parseAngleForBin(0U, attributes), -90.0f, 0.001f);
    EXPECT_NEAR(parseAngleForBin(89U, attributes), 90.0f, 0.001f);
}
