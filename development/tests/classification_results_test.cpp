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

#include "opk/Labels.h"
#include "postproc/CameraContactParser.h"
#include "postproc/ImageNetClassificationParser.h"
#include "postproc/PersonClassificationParser.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>

TEST(ClassificationResults, ContactMappingsAndUnavailableScoresRoundTrip) {
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto inf = std::numeric_limits<float>::infinity();
    for (const int contactIndex : {0, 1}) {
        for (const auto &values : {std::array<float, 2>{5, -5}, {0, 0}, {nan, nan}, {inf, inf}}) {
            opk::AttributeMap attributes;
            attributes.set("contactClassIndex", static_cast<int64_t>(contactIndex));
            attributes.set("noContactClassIndex", static_cast<int64_t>(1 - contactIndex));
            opk::TensorView tensor(
                values.data(), sizeof(values), opk::Shape(1, 2), opk::Dtype::Float32, 1, 0);
            opk::TensorParser::Input input(attributes);
            input.tensors[0] = &tensor;
            input.inferenceInfo.parentId = 42;
            open_perception_kit::FrameResults results;
            opk::stdop::postproc::CameraContactParser parser;
            ASSERT_TRUE(parser.parse(input, results));
            EXPECT_EQ(results.count<open_perception_kit::metadata::ClassificationsT>(), 0U);
            const open_perception_kit::FrameResults decoded(
                open_perception_kit::serialize(results));
            EXPECT_EQ(decoded.count<open_perception_kit::metadata::CameraContactsT>(), 1U);
            decoded.for_each<open_perception_kit::metadata::CameraContactsT>(
                [&](const auto &payload) {
                    ASSERT_EQ(payload.contacts.size(), 1U);
                    EXPECT_EQ(payload.contacts[0]->object->parent_id, 42U);
                    EXPECT_EQ(payload.contacts[0]->subject_id, 42U);
                    if (!std::isfinite(values[0]) || !std::isfinite(values[1])) {
                        EXPECT_EQ(payload.contacts[0]->result, nullptr);
                        return;
                    }
                    ASSERT_NE(payload.contacts[0]->result, nullptr);
                    const auto &selected = *payload.contacts[0]->result;
                    if (values[0] == 5) {
                        EXPECT_EQ(selected.model_class_index, 0);
                        EXPECT_EQ(selected.in_contact, contactIndex == 0);
                        EXPECT_FLOAT_EQ(selected.confidence.value_or(0),
                                        1.0f / (1.0f + std::exp(-10.0f)));
                    } else {
                        EXPECT_EQ(selected.model_class_index, contactIndex);
                        EXPECT_TRUE(selected.in_contact);
                        EXPECT_FLOAT_EQ(selected.confidence.value_or(0), .5f);
                    }
                });
        }
    }
}

TEST(ClassificationResults, PersonDecisionAndRawScoresNeedNoInputFormatGuess) {
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto inf = std::numeric_limits<float>::infinity();
    for (const auto &values : {std::array<float, 2>{.8f, nan},
                               {inf, inf},
                               {0, 1},
                               {0, 0},
                               {2, -1},
                               {.8f, .8f},
                               {.8f, .2f}}) {
        const opk::AttributeMap attributes;
        opk::TensorView tensor(
            values.data(), sizeof(values), opk::Shape(1, 2), opk::Dtype::Float32, 1, 0);
        opk::TensorParser::Input input(attributes);
        input.tensors[0] = &tensor;
        input.inferenceInfo.parentId = 42;
        open_perception_kit::FrameResults results;
        opk::stdop::postproc::PersonClassificationParser parser;
        ASSERT_TRUE(parser.parse(input, results));
        const open_perception_kit::FrameResults decoded(open_perception_kit::serialize(results));
        ASSERT_EQ(decoded.count<open_perception_kit::metadata::PersonPresencesT>(), 1U);
        decoded.for_each<open_perception_kit::metadata::PersonPresencesT>([&](const auto &payload) {
            ASSERT_EQ(payload.presences.size(), 1U);
            const auto &presence = *payload.presences[0];
            EXPECT_EQ(presence.subject_id, 42U);
            if (!std::isfinite(values[0]) || !std::isfinite(values[1])) {
                EXPECT_EQ(presence.result, nullptr);
            } else {
                ASSERT_NE(presence.result, nullptr);
                EXPECT_EQ(presence.result->person_present, values[0] > values[1]);
                EXPECT_EQ(presence.result->yes_score, values[0]);
                EXPECT_EQ(presence.result->no_score, values[1]);
            }
        });
    }
}

TEST(ClassificationResults, ImageNetIdentityOrderingAndEmptyOutputArePreserved) {
    constexpr auto count =
        opk::resources::Labels::getLabelCount(opk::resources::LabelType::ImageNet);
    std::array<float, count> values{};
    values[2] = 10;
    values[7] = 10;
    opk::AttributeMap attributes;
    attributes.set("topK", int64_t{2});
    opk::TensorView tensor(values.data(),
                           sizeof(values),
                           opk::Shape(1, static_cast<int>(count)),
                           opk::Dtype::Float32,
                           1,
                           0);
    opk::TensorParser::Input input(attributes);
    input.tensors[0] = &tensor;
    input.inferenceInfo.parentId = 42;
    open_perception_kit::FrameResults results;
    opk::stdop::postproc::ImageNetClassificationParser parser;
    ASSERT_TRUE(parser.parse(input, results));
    const open_perception_kit::FrameResults decoded(open_perception_kit::serialize(results));
    EXPECT_EQ(decoded.count<open_perception_kit::metadata::ClassificationsT>(), 1U);
    decoded.for_each<open_perception_kit::metadata::ClassificationsT>([](const auto &payload) {
        ASSERT_EQ(payload.classifications.size(), 1U);
        const auto &classification = *payload.classifications[0];
        EXPECT_EQ(classification.label_set, "opk:imagenet-1001");
        EXPECT_EQ(classification.kind,
                  open_perception_kit::metadata::ClassificationKind::Categorical);
        EXPECT_EQ(classification.subject_id, 42U);
        ASSERT_EQ(classification.candidates.size(), 2U);
        EXPECT_EQ(classification.candidates[0]->class_id, 7);
        EXPECT_EQ(classification.candidates[1]->class_id, 2);
        EXPECT_EQ(classification.candidates[0]->text,
                  opk::resources::Labels::getLabel(opk::resources::LabelType::ImageNet, 7));
        EXPECT_EQ(classification.candidates[0]->confidence,
                  classification.candidates[1]->confidence);
        EXPECT_TRUE(classification.candidates[0]->confidence.has_value());
    });
    values.fill(0);
    values[0] = 10;
    attributes.set("topK", int64_t{1});
    open_perception_kit::FrameResults background;
    ASSERT_TRUE(parser.parse(input, background));
    const open_perception_kit::FrameResults decodedBackground(
        open_perception_kit::serialize(background));
    decodedBackground.for_each<open_perception_kit::metadata::ClassificationsT>(
        [](const auto &payload) {
            ASSERT_EQ(payload.classifications.size(), 1U);
            ASSERT_EQ(payload.classifications[0]->candidates.size(), 1U);
            EXPECT_EQ(payload.classifications[0]->candidates[0]->class_id, 0U);
        });
    values.fill(0);
    for (const auto threshold : {.01, 1.0}) {
        attributes.set("confidenceThreshold", threshold);
        open_perception_kit::FrameResults empty;
        ASSERT_TRUE(parser.parse(input, empty));
        const open_perception_kit::FrameResults decodedEmpty(open_perception_kit::serialize(empty));
        ASSERT_EQ(decodedEmpty.count<open_perception_kit::metadata::ClassificationsT>(), 1U);
        decodedEmpty.for_each<open_perception_kit::metadata::ClassificationsT>(
            [](const auto &payload) {
                ASSERT_TRUE(payload.layer);
                ASSERT_EQ(payload.classifications.size(), 1U);
                const auto &result = *payload.classifications[0];
                EXPECT_EQ(result.kind,
                          open_perception_kit::metadata::ClassificationKind::Categorical);
                EXPECT_EQ(result.label_set, "opk:imagenet-1001");
                EXPECT_EQ(result.subject_id, 42U);
                EXPECT_TRUE(result.candidates.empty());
            });
    }
    for (const float invalid : {std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::infinity(),
                                -std::numeric_limits<float>::infinity()}) {
        values[0] = invalid;
        open_perception_kit::FrameResults bad;
        EXPECT_FALSE(parser.parse(input, bad));
        EXPECT_EQ(bad.count<open_perception_kit::metadata::ClassificationsT>(), 0U);
    }
    values.fill(0);
    for (const auto threshold : {-1.0, 2.0, std::numeric_limits<double>::quiet_NaN()}) {
        attributes.set("confidenceThreshold", threshold);
        open_perception_kit::FrameResults bad;
        EXPECT_FALSE(parser.parse(input, bad));
        EXPECT_TRUE(bad.empty());
    }
    attributes.set("confidenceThreshold", .01).set("topK", int64_t{-1});
    open_perception_kit::FrameResults bad;
    EXPECT_FALSE(parser.parse(input, bad));
    EXPECT_TRUE(bad.empty());
}
