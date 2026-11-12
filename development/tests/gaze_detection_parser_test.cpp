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

    const auto gazeRef = output.get<open_perception_kit::metadata::GazeMeasurementsT>();
    if (!gazeRef) {
        ADD_FAILURE() << "missing GazeMeasurements payload";
        return std::numeric_limits<float>::quiet_NaN();
    }
    const auto &measurements = gazeRef.value().value().measurements;
    if (measurements.size() != 1U || !measurements[0] || !measurements[0]->angles) {
        ADD_FAILURE() << "expected one available gaze measurement";
        return std::numeric_limits<float>::quiet_NaN();
    }
    EXPECT_FALSE(measurements[0]->subject_id.has_value());
    EXPECT_EQ(measurements[0]->confidence, 1.0f);
    return measurements[0]->angles->yaw_degrees;
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

TEST(GazeDetectionParser, UsesDefaultMappingWithoutAngleBinWidth) {
    const opk::AttributeMap attributes;
    EXPECT_NEAR(parseAngleForBin(0U, attributes), -90.0f, 0.001f);
    EXPECT_NEAR(parseAngleForBin(89U, attributes), 90.0f, 0.001f);
}

TEST(GazeDetectionParser, InvalidAxisMakesThePairUnavailableWithoutLosingSubject) {
    opk::AttributeMap attributes;
    attributes.set("angleBinWidthDeg", 4.0);
    for (const float invalid : {std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::infinity(),
                                -std::numeric_limits<float>::infinity()}) {
        for (const int axis : {0, 1}) {
            std::array<float, 90> yaw{}, pitch{};
            yaw[45] = pitch[45] = 100;
            (axis == 0 ? yaw : pitch)[10] = invalid;
            opk::TensorView y(
                yaw.data(), sizeof(yaw), opk::Shape(1, 90), opk::Dtype::Float32, 1, 0);
            opk::TensorView p(
                pitch.data(), sizeof(pitch), opk::Shape(1, 90), opk::Dtype::Float32, 1, 0);
            opk::TensorParser::Input input(attributes);
            input.tensors[0] = &y;
            input.tensors[1] = &p;
            input.inferenceInfo.parentId = 42;
            open_perception_kit::FrameResults result;
            opk::stdop::postproc::GazeDetectionParser parser;
            ASSERT_TRUE(parser.parse(input, result));
            const open_perception_kit::FrameResults decoded(open_perception_kit::serialize(result));
            EXPECT_EQ(decoded.count<open_perception_kit::metadata::GazeMeasurementsT>(), 1U);
            decoded.for_each<open_perception_kit::metadata::GazeMeasurementsT>(
                [](const auto &payload) {
                    ASSERT_EQ(payload.measurements.size(), 1U);
                    EXPECT_EQ(payload.measurements[0]->angles, nullptr);
                    EXPECT_FALSE(payload.measurements[0]->confidence.has_value());
                    EXPECT_EQ(payload.measurements[0]->subject_id, 42U);
                    EXPECT_EQ(payload.measurements[0]->object->parent_id, 42U);
                });
        }
    }
}

TEST(GazeDetectionParser, ConfidenceIsMinimumBinProbabilityAndInvalidConfigIsAnError) {
    std::array<float, 90> yaw{}, pitch{};
    yaw[45] = 100;
    opk::TensorView y(yaw.data(), sizeof(yaw), opk::Shape(1, 90), opk::Dtype::Float32, 1, 0);
    opk::TensorView p(pitch.data(), sizeof(pitch), opk::Shape(1, 90), opk::Dtype::Float32, 1, 0);
    opk::AttributeMap attributes;
    attributes.set("angleBinWidthDeg", 4.0);
    opk::TensorParser::Input input(attributes);
    input.tensors[0] = &y;
    input.tensors[1] = &p;
    opk::stdop::postproc::GazeDetectionParser parser;
    open_perception_kit::FrameResults result;
    ASSERT_TRUE(parser.parse(input, result));
    result.for_each<open_perception_kit::metadata::GazeMeasurementsT>([](const auto &payload) {
        ASSERT_EQ(payload.measurements.size(), 1U);
        const auto &gaze = *payload.measurements[0];
        ASSERT_NE(gaze.angles, nullptr);
        EXPECT_FLOAT_EQ(gaze.angles->yaw_degrees, 0);
        EXPECT_NEAR(gaze.angles->pitch_degrees, -2, .001);
        EXPECT_NEAR(gaze.confidence.value_or(1), 1.0f / 90.0f, 1e-6);
    });
    for (const double width : {0.0,
                               -1.0,
                               std::numeric_limits<double>::infinity(),
                               std::numeric_limits<double>::quiet_NaN()}) {
        attributes.set("angleBinWidthDeg", width);
        open_perception_kit::FrameResults invalid;
        EXPECT_FALSE(parser.parse(input, invalid));
    }
}
