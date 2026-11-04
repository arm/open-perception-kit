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

#include "postproc/PaddleOcrDbDetectionParser.h"
#include "postproc/PaddleOcrRecognitionParser.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

TEST(PaddleOcrDbDetectionParser, RestoresSourcePixelBoxAndParentFromPaddedProbabilityMap) {
    opk::AttributeMap attributes;
    attributes.set("parser", "PaddleOcrDbDetectionParser");
    attributes.set("binaryThreshold", 0.3);
    attributes.set("confidenceThreshold", 0.5);
    attributes.set("unclipRatio", 1.5);
    attributes.set("maxCandidates", int64_t{10});
    attributes.set("minBoxSize", 1.0);
    attributes.set("minExpandedBoxSize", 1.0);

    std::vector<float> probabilities(16U * 16U, 0.0f);
    for (size_t y = 6; y <= 8; ++y)
        for (size_t x = 5; x <= 9; ++x)
            probabilities[y * 16 + x] = 0.9f;
    opk::TensorView tensor(probabilities.data(),
                           probabilities.size() * sizeof(float),
                           opk::Shape(1, 1, 16, 16),
                           opk::Dtype::Float32,
                           1.0f,
                           0.0f);
    opk::TensorParser::Input input(attributes);
    input.tensors[0] = &tensor;
    input.inferenceInfo.image = {.width = 192,
                                 .height = 128,
                                 .modelWidth = 64,
                                 .modelHeight = 64,
                                 .letterboxLeft = 8,
                                 .letterboxRight = 8,
                                 .letterboxTop = 16,
                                 .letterboxBottom = 16};
    input.inferenceInfo.parentId = 42;

    open_perception_kit::FrameResults results;
    opk::stdop::postproc::PaddleOcrDbDetectionParser parser;
    const auto parsed = parser.parse(input, results);
    ASSERT_TRUE(parsed) << parsed.error().toString();
    ASSERT_EQ(results.count<open_perception_kit::metadata::BoxDetectionsT>(), 1U);
    const auto payload = results.get<open_perception_kit::metadata::BoxDetectionsT>();
    ASSERT_TRUE(payload.has_value());
    ASSERT_TRUE(payload->value().layer);
    EXPECT_EQ(payload->value().layer->content_type, "textRegion");
    const auto &detections = payload->value().detections;
    ASSERT_EQ(detections.size(), 1U);
    ASSERT_TRUE(detections[0]);
    const auto &detection = *detections[0];
    ASSERT_TRUE(detection.object);
    EXPECT_EQ(detection.object->parent_id, 42U);
    ASSERT_TRUE(detection.box);
    // The 4x2 contour expands one map pixel per edge: [4,5]..[10,9].
    // Scale by four, remove (8,16) model padding, then scale by four again.
    EXPECT_FLOAT_EQ(detection.box->x, 32.0f);
    EXPECT_FLOAT_EQ(detection.box->y, 16.0f);
    EXPECT_FLOAT_EQ(detection.box->width, 96.0f);
    EXPECT_FLOAT_EQ(detection.box->height, 64.0f);
    EXPECT_FLOAT_EQ(detection.confidence, 0.9f);
}

class PaddleOcrRecognitionParserTest : public ::testing::Test {
  protected:
    void SetUp() override {
        ASSERT_TRUE(std::filesystem::create_directory(directory));
        std::ofstream dictionary(dictionaryFile);
        ASSERT_TRUE(dictionary.is_open());
        dictionary << "A\n";
        dictionary.close();
        ASSERT_TRUE(dictionary);
    }

    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("opk-paddle-ocr-parser-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::filesystem::path dictionaryFile = directory / "characters.txt";
};

TEST_F(PaddleOcrRecognitionParserTest, CollapsesAdjacentRepeatsBeforeRemovingBlank) {
    opk::AttributeMap attributes;
    attributes.set("parser", "PaddleOcrRecognitionParser");
    attributes.set("dictionaryFile", dictionaryFile.string());
    // Dictionary A gets class 1, between the implicit blank and space classes.
    const std::array<float, 12> probabilities{
        0.05f,
        0.9f,
        0.05f, // A
        0.2f,
        0.6f,
        0.2f, // A (adjacent repeat)
        0.8f,
        0.1f,
        0.1f, // blank
        0.2f,
        0.7f,
        0.1f, // A (retained after blank)
    };
    opk::TensorView tensor(probabilities.data(),
                           probabilities.size() * sizeof(float),
                           opk::Shape(1, 4, 3),
                           opk::Dtype::Float32,
                           1.0f,
                           0.0f);
    opk::TensorParser::Input input(attributes);
    input.tensors[0] = &tensor;
    input.inferenceInfo.parentId = 42;

    open_perception_kit::FrameResults results;
    opk::stdop::postproc::PaddleOcrRecognitionParser parser;
    const auto parsed = parser.parse(input, results);
    ASSERT_TRUE(parsed) << parsed.error().toString();
    ASSERT_EQ(results.count<open_perception_kit::metadata::ClassificationsT>(), 1U);
    const auto payload = results.get<open_perception_kit::metadata::ClassificationsT>();
    ASSERT_TRUE(payload.has_value());
    ASSERT_TRUE(payload->value().layer);
    EXPECT_EQ(payload->value().layer->content_type, "textRecognition");
    const auto &classifications = payload->value().classifications;
    ASSERT_EQ(classifications.size(), 1U);
    ASSERT_TRUE(classifications[0]);
    ASSERT_TRUE(classifications[0]->object);
    EXPECT_EQ(classifications[0]->object->parent_id, 42U);
    const auto &candidates = classifications[0]->candidates;
    ASSERT_EQ(candidates.size(), 1U);
    ASSERT_TRUE(candidates[0]);
    EXPECT_EQ(candidates[0]->text, "AA");
    // Only retained characters contribute: (0.9 + 0.7) / 2, excluding repeat/blank.
    EXPECT_FLOAT_EQ(candidates[0]->confidence, 0.8f);
}

} // namespace
