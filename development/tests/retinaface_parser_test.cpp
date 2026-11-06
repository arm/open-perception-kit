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

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include "postproc/RetinaFaceParser.h"

namespace {

struct RetinaFaceFixture {
    std::vector<float> boxes, scores, landmarks;
    opk::TensorView boxView, scoreView, landmarkView;
    opk::AttributeMap attributes;
    opk::TensorParser::Input input{attributes};
    open_perception_kit::FrameResults output;
    opk::stdop::postproc::RetinaFaceParser parser;

    RetinaFaceFixture(int width = 32, int height = 32, int count = 42)
        : boxes(static_cast<size_t>(count) * 4), scores(static_cast<size_t>(count) * 2),
          landmarks(static_cast<size_t>(count) * 10), boxView(boxes.data(),
                                                              boxes.size() * sizeof(float),
                                                              opk::Shape(1, count, 4),
                                                              opk::Dtype::Float32,
                                                              1,
                                                              0),
          scoreView(scores.data(),
                    scores.size() * sizeof(float),
                    opk::Shape(1, count, 2),
                    opk::Dtype::Float32,
                    1,
                    0),
          landmarkView(landmarks.data(),
                       landmarks.size() * sizeof(float),
                       opk::Shape(1, count, 10),
                       opk::Dtype::Float32,
                       1,
                       0) {
        input.tensors[0] = &boxView;
        input.tensors[1] = &scoreView;
        input.tensors[2] = &landmarkView;
        input.inferenceInfo.image = {.width = 64,
                                     .height = 96,
                                     .modelWidth = static_cast<size_t>(width),
                                     .modelHeight = static_cast<size_t>(height),
                                     .frameId = 7,
                                     .frameWidth = 64,
                                     .frameHeight = 96};
        input.inferenceInfo.parentId = 42;
    }
};

TEST(RetinaFaceParser, DecodesBoxesSuppressesDuplicatesAndScalesToFrame) {
    RetinaFaceFixture fixture;
    fixture.scores[10 * 2 + 1] = 0.9f;
    fixture.scores[11 * 2 + 1] = 0.8f;
    fixture.boxes[11 * 4 + 2] = std::log(0.5f) / 0.2f;
    fixture.boxes[11 * 4 + 3] = std::log(0.5f) / 0.2f;
    ASSERT_TRUE(fixture.parser.parse(fixture.input, fixture.output));
    const auto result = fixture.output.get<open_perception_kit::metadata::BoxDetectionsT>();
    ASSERT_TRUE(result);
    const auto &detections = result.value().value().detections;
    ASSERT_EQ(detections.size(), 1);
    ASSERT_TRUE(detections[0]->object);
    EXPECT_EQ(detections[0]->object->parent_id, 42);
    EXPECT_FLOAT_EQ(detections[0]->confidence, 0.9f);
    EXPECT_FLOAT_EQ(detections[0]->box->x, 8);
    EXPECT_FLOAT_EQ(detections[0]->box->y, 12);
    EXPECT_FLOAT_EQ(detections[0]->box->width, 32);
    EXPECT_FLOAT_EQ(detections[0]->box->height, 48);
    EXPECT_EQ(detections[0]->class_id, 0U);
    EXPECT_EQ(detections[0]->box->space->frame_id, 7U);
    EXPECT_EQ(detections[0]->box->units, open_perception_kit::metadata::CoordinateUnits::Pixels);
}

TEST(RetinaFaceParser, CroppedBoxesProjectThroughTheirDeclaredSourceImage) {
    RetinaFaceFixture fixture;
    auto &image = fixture.input.inferenceInfo.image;
    image.frameWidth = image.frameHeight = 256;
    image.cropX = 10;
    image.cropY = 20;
    fixture.scores[21] = 0.9f;
    ASSERT_TRUE(fixture.parser.parse(fixture.input, fixture.output));
    const auto result = fixture.output.get<open_perception_kit::metadata::BoxDetectionsT>();
    if (!result) {
        ADD_FAILURE() << "Expected RetinaFace output";
        return;
    }
    ASSERT_EQ(result.value().value().detections.size(), 1U);
    const auto &box = *result.value().value().detections[0]->box;
    const auto projected = open_perception_kit::projectBoundingBox(box, 256, 256);
    if (!projected) {
        ADD_FAILURE() << "Expected projectable RetinaFace geometry";
        return;
    }
    EXPECT_EQ(*projected, (std::array<double, 4>{18, 32, 32, 48}));
}

TEST(RetinaFaceParser, AppliesOffsetVarianceAndClipsToFrame) {
    RetinaFaceFixture fixture;
    fixture.scores[1] = 0.9f;
    fixture.boxes[0] = 1.0f;
    fixture.boxes[1] = 2.0f;
    ASSERT_TRUE(fixture.parser.parse(fixture.input, fixture.output));
    const auto result = fixture.output.get<open_perception_kit::metadata::BoxDetectionsT>();
    ASSERT_TRUE(result);
    const auto &box = *result.value().value().detections[0]->box;
    EXPECT_FLOAT_EQ(box.x, 0);
    EXPECT_FLOAT_EQ(box.y, 0);
    EXPECT_NEAR(box.width, 27.2f, 1e-5f);
    EXPECT_NEAR(box.height, 45.6f, 1e-5f);
}

TEST(RetinaFaceParser, SupportsBothRequestedAnchorCounts) {
    for (const auto [width, count] : {std::pair{1080, 48010}, std::pair{1920, 85200}}) {
        RetinaFaceFixture fixture(width, 1080, count);
        fixture.scores[(count - 1) * 2 + 1] = 0.9f;
        ASSERT_TRUE(fixture.parser.parse(fixture.input, fixture.output));
        ASSERT_TRUE(fixture.output.get<open_perception_kit::metadata::BoxDetectionsT>());
    }
}

TEST(RetinaFaceParser, PreservesScoreAndAnchorOrderAtCandidateLimit) {
    for (const size_t count : {4999U, 5000U, 5001U}) {
        for (const float lastScore : {0.9f, 0.95f}) {
            SCOPED_TRACE(count);
            SCOPED_TRACE(lastScore);
            RetinaFaceFixture fixture(1080, 1080, 48010);
            fixture.input.inferenceInfo.image.width = 1080;
            fixture.input.inferenceInfo.image.height = 1080;
            fixture.input.inferenceInfo.image.frameWidth = 1080;
            fixture.input.inferenceInfo.image.frameHeight = 1080;
            // Overlapping boxes leave one distinct final anchor to expose the cutoff and ties.
            for (size_t index = 0; index < count; ++index) {
                const float size = index % 2 == 0 ? 16.0f : 32.0f;
                const float center = index == count - 1 ? 300.0f : 100.0f;
                fixture.scores[2 * index + 1] = index == count - 1 ? lastScore : 0.9f;
                const size_t cell = index / 2;
                const size_t x = cell % 135;
                const size_t y = cell / 135;
                fixture.boxes[4 * index] =
                    (center - (static_cast<float>(x) + 0.5f) * 8) / (0.1f * size);
                fixture.boxes[4 * index + 1] =
                    (center - (static_cast<float>(y) + 0.5f) * 8) / (0.1f * size);
                fixture.boxes[4 * index + 2] = std::log(16.0f / size) / 0.2f;
                fixture.boxes[4 * index + 3] = std::log(16.0f / size) / 0.2f;
            }
            ASSERT_TRUE(fixture.parser.parse(fixture.input, fixture.output));
            const auto result = fixture.output.get<open_perception_kit::metadata::BoxDetectionsT>();
            ASSERT_TRUE(result);
            const auto &detections = result.value().value().detections;
            ASSERT_EQ(detections.size(), count > 5000 && lastScore == 0.9f ? 1 : 2);
            EXPECT_FLOAT_EQ(detections[0]->confidence, lastScore);
            EXPECT_NEAR(detections[0]->box->x, lastScore == 0.9f ? 92.0f : 292.0f, 1e-3f);
            if (detections.size() == 2)
                EXPECT_NEAR(detections[1]->box->x, lastScore == 0.9f ? 292.0f : 92.0f, 1e-3f);
            fixture.boxes[2] = std::numeric_limits<float>::infinity();
            open_perception_kit::FrameResults filtered;
            ASSERT_TRUE(fixture.parser.parse(fixture.input, filtered));
            const auto filteredResult =
                filtered.get<open_perception_kit::metadata::BoxDetectionsT>();
            ASSERT_TRUE(filteredResult);
            EXPECT_EQ(filteredResult.value().value().detections.size(), 2);
        }
    }
}

TEST(RetinaFaceParser, SuppressesHugeFiniteOverlappingBoxes) {
    RetinaFaceFixture fixture;
    for (const size_t index : {0U, 2U}) {
        fixture.scores[2 * index + 1] = index == 0 ? 0.9f : 0.8f;
        fixture.boxes[4 * index + 2] = 225.0f;
        fixture.boxes[4 * index + 3] = 225.0f;
    }
    ASSERT_TRUE(fixture.parser.parse(fixture.input, fixture.output));
    const auto result = fixture.output.get<open_perception_kit::metadata::BoxDetectionsT>();
    ASSERT_TRUE(result);
    const auto &detections = result.value().value().detections;
    ASSERT_EQ(detections.size(), 1);
    EXPECT_FLOAT_EQ(detections[0]->confidence, 0.9f);
    EXPECT_FLOAT_EQ(detections[0]->box->width, 64);
    EXPECT_FLOAT_EQ(detections[0]->box->height, 96);
}

TEST(RetinaFaceParser, UsesInclusivePixelIoUForPartialOverlap) {
    RetinaFaceFixture fixture;
    // [0,0,2,2] and [1,0,3,2]: inclusive IoU is 0.5; continuous IoU is 1/3.
    for (const size_t index : {0U, 2U}) {
        fixture.scores[2 * index + 1] = index == 0 ? 0.9f : 0.8f;
        fixture.boxes[4 * index] = index == 0 ? -3.0f / 1.6f : -10.0f / 1.6f;
        fixture.boxes[4 * index + 1] = -3.0f / 1.6f;
        fixture.boxes[4 * index + 2] = std::log(2.0f / 16.0f) / 0.2f;
        fixture.boxes[4 * index + 3] = std::log(2.0f / 16.0f) / 0.2f;
    }
    ASSERT_TRUE(fixture.parser.parse(fixture.input, fixture.output));
    const auto result = fixture.output.get<open_perception_kit::metadata::BoxDetectionsT>();
    ASSERT_TRUE(result);
    ASSERT_EQ(result.value().value().detections.size(), 1);
    EXPECT_FLOAT_EQ(result.value().value().detections[0]->confidence, 0.9f);
}

TEST(RetinaFaceParser, CapsKeptBoxesAndPreservesAnchorOrder) {
    for (const size_t count : {749U, 750U, 751U}) {
        SCOPED_TRACE(count);
        RetinaFaceFixture fixture(320, 320, 4200);
        fixture.input.inferenceInfo.image.width = 320;
        fixture.input.inferenceInfo.image.height = 320;
        fixture.input.inferenceInfo.image.frameWidth = 320;
        fixture.input.inferenceInfo.image.frameHeight = 320;
        for (size_t index = 0; index < count; ++index) {
            const size_t anchor = 2 * index;
            fixture.scores[2 * anchor + 1] = 0.9f;
            fixture.boxes[4 * anchor + 2] = std::log(1.0f / 16.0f) / 0.2f;
            fixture.boxes[4 * anchor + 3] = std::log(1.0f / 16.0f) / 0.2f;
        }
        ASSERT_TRUE(fixture.parser.parse(fixture.input, fixture.output));
        const auto result = fixture.output.get<open_perception_kit::metadata::BoxDetectionsT>();
        ASSERT_TRUE(result);
        const auto &detections = result.value().value().detections;
        ASSERT_EQ(detections.size(), std::min(count, size_t{750}));
        for (size_t index = 0; index < detections.size(); ++index) {
            const size_t x = index % 40;
            const size_t y = index / 40;
            EXPECT_FLOAT_EQ(detections[index]->box->x, 8.0f * static_cast<float>(x) + 3.5f);
            EXPECT_FLOAT_EQ(detections[index]->box->y, 8.0f * static_cast<float>(y) + 3.5f);
        }
    }
}

TEST(RetinaFaceParser, RejectsWrongHeadOrderAndMissingLandmarks) {
    RetinaFaceFixture fixture;
    std::swap(fixture.input.tensors[0], fixture.input.tensors[1]);
    EXPECT_FALSE(fixture.parser.parse(fixture.input, fixture.output));
    std::swap(fixture.input.tensors[0], fixture.input.tensors[1]);
    fixture.input.tensors[2] = nullptr;
    EXPECT_FALSE(fixture.parser.parse(fixture.input, fixture.output));
}

TEST(RetinaFaceParser, RejectsWrongAnchorCountAndLetterboxing) {
    RetinaFaceFixture fixture(32, 32, 41);
    EXPECT_FALSE(fixture.parser.parse(fixture.input, fixture.output));
    RetinaFaceFixture padded;
    padded.input.inferenceInfo.image.letterboxLeft = 1;
    EXPECT_FALSE(padded.parser.parse(padded.input, padded.output));
}

TEST(RetinaFaceParser, IgnoresNonFiniteAndLowConfidenceCandidates) {
    RetinaFaceFixture fixture;
    fixture.scores[1] = std::numeric_limits<float>::quiet_NaN();
    fixture.scores[3] = 0.9f;
    fixture.boxes[6] = std::numeric_limits<float>::infinity();
    fixture.scores[5] = 0.5f;
    EXPECT_TRUE(fixture.parser.parse(fixture.input, fixture.output));
    const auto result = fixture.output.get<open_perception_kit::metadata::BoxDetectionsT>();
    if (!result) {
        ADD_FAILURE() << "Expected explicit empty RetinaFace output";
        return;
    }
    EXPECT_TRUE(result.value().value().detections.empty());
}

} // namespace
