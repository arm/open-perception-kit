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

#include "FrameInputs.h"
#include "opk/Embedding.h"
#include "postproc/ObjectEmbeddingParser.h"
#include <gtest/gtest.h>
#include <limits>

namespace {
using namespace open_perception_kit;
using namespace open_perception_kit::metadata;

ObjectEmbeddingT embedding(const std::string &space = "test:a",
                           const std::vector<float> &values = {1, 0}) {
    ObjectEmbeddingT result;
    result.object = makeObjectMeta(50, 999);
    result.subject_id = 7;
    result.values = values;
    result.space = std::make_unique<EmbeddingSpaceT>();
    result.space->id = space;
    result.space->dimensions = static_cast<uint32_t>(values.size());
    result.space->metric = EmbeddingMetric::Cosine;
    return result;
}

FrameResults frame(const uint64_t id = 7, const float x = 0) {
    BoxDetectionsT boxes;
    boxes.layer = makeLayerInfo({.contentType = "genericObject"});
    auto box = std::make_unique<BoxDetectionT>();
    box->object = makeObjectMeta(id);
    box->box = makeImageBoundingBox(
        {.width = 100, .height = 100, .frameId = 1, .frameWidth = 100, .frameHeight = 100},
        x,
        0,
        10,
        10);
    boxes.detections.push_back(std::move(box));
    FrameResults result;
    result.add(std::move(boxes));
    return result;
}

void addEmbedding(FrameResults &result,
                  const ObjectEmbeddingT &value,
                  const std::string &producer = "one") {
    ObjectEmbeddingsT payload;
    payload.layer = makeLayerInfo({.contentType = "custom-not-routing"});
    payload.layer->producer = makeProducerInfo(producer, "test/Embedding", "fixture");
    payload.embeddings.push_back(std::make_unique<ObjectEmbeddingT>(value));
    result.add(std::move(payload));
}

TEST(EmbeddingContract, CompatibilityIsNotLengthOrProducerNameAndMathIsFinite) {
    const auto a = embedding();
    EXPECT_FLOAT_EQ(opk::embedding::cosineSimilarity(a, a).value_or(-2), 1);
    EXPECT_FALSE(opk::embedding::cosineSimilarity(a, embedding("test:b")));
    EXPECT_FALSE(opk::embedding::cosineSimilarity(a, embedding("test:a", {1})));
    EXPECT_FLOAT_EQ(opk::embedding::cosineSimilarity(a, embedding("test:a", {0, 1})).value_or(-2),
                    0);
    EXPECT_FLOAT_EQ(opk::embedding::cosineSimilarity(a, embedding("test:a", {-1, 0})).value_or(-2),
                    -1);
    for (const float value :
         {std::numeric_limits<float>::max(), std::numeric_limits<float>::denorm_min()}) {
        const auto largeOrSmall = embedding("test:a", {value, value});
        EXPECT_TRUE(opk::embedding::valid(largeOrSmall));
        EXPECT_NEAR(
            opk::embedding::cosineSimilarity(largeOrSmall, largeOrSmall).value_or(-2), 1, 1e-6);
    }
    for (const auto &values : {std::vector<float>{},
                               std::vector<float>{0, 0},
                               std::vector<float>{1, std::numeric_limits<float>::quiet_NaN()}}) {
        EXPECT_FALSE(opk::embedding::cosineSimilarity(a, embedding("test:a", values)));
    }
    auto invalid = a;
    invalid.space->dimensions = 3;
    EXPECT_FALSE(opk::embedding::valid(invalid));
    invalid = a;
    invalid.space->metric = static_cast<EmbeddingMetric>(99);
    EXPECT_FALSE(opk::embedding::valid(invalid));
    EXPECT_FALSE(opk::embedding::valid(embedding("")));
}

TEST(EmbeddingContract, SelectionRejectsDuplicatesBeforeValidityAndHonorsExplicitFilters) {
    for (const bool reverse : {false, true}) {
        auto result = frame();
        const auto a = embedding("test:a");
        const auto b = embedding("test:b");
        addEmbedding(result, reverse ? b : a, reverse ? "two" : "one");
        addEmbedding(result, reverse ? a : b, reverse ? "one" : "two");
        opk::tracker::Config config;
        EXPECT_EQ(opk::tracker::frameinputs::collectEmbeddings(result, config).at(7), nullptr);
        config.embeddingSpaceId = "test:a";
        const auto selected = opk::tracker::frameinputs::collectEmbeddings(result, config);
        ASSERT_NE(selected.at(7), nullptr);
        EXPECT_EQ(selected.at(7)->space->id, "test:a");
        config.embeddingProducerId = "two";
        EXPECT_TRUE(opk::tracker::frameinputs::collectEmbeddings(result, config).empty());
        config.embeddingSpaceId.clear();
        EXPECT_NE(opk::tracker::frameinputs::collectEmbeddings(result, config).at(7), nullptr);
        addEmbedding(result, embedding("test:b", {0, 0}), "two");
        EXPECT_EQ(opk::tracker::frameinputs::collectEmbeddings(result, config).at(7), nullptr);
    }
    auto result = frame();
    auto absent = embedding();
    absent.subject_id = flatbuffers::nullopt;
    absent.object->parent_id = 7;
    addEmbedding(result, absent);
    EXPECT_TRUE(opk::tracker::frameinputs::collectEmbeddings(result, {}).empty());
    addEmbedding(result, embedding());
    EXPECT_NE(opk::tracker::frameinputs::collectEmbeddings(result, {}).at(7), nullptr);
    const auto duplicate = frame();
    duplicate.for_each<BoxDetectionsT>([&](const auto &boxes) { result.add(boxes); });
    EXPECT_EQ(opk::tracker::frameinputs::collectEmbeddings(result, {}).at(7), nullptr);
}

TEST(EmbeddingContract, ParserPublishesSpaceAndSubjectAndRejectsMalformedTensors) {
    opk::AttributeMap attributes;
    attributes.set("embeddingSpaceId", "test:a");
    opk::TensorParser::Input input(attributes);
    input.inferenceInfo.parentId = 7;
    std::vector<float> values{3, 4};
    opk::TensorView tensor(
        values.data(), values.size() * sizeof(float), opk::Shape(1, 2), opk::Dtype::Float32, 1, 0);
    input.tensors[0] = &tensor;
    opk::stdop::postproc::ObjectEmbeddingParser parser;
    FrameResults result;
    ASSERT_TRUE(parser.parse(input, result));
    FrameResults decoded(serialize(result));
    decoded.for_each<ObjectEmbeddingsT>([](const auto &payload) {
        ASSERT_EQ(payload.embeddings.size(), 1U);
        const auto &value = *payload.embeddings[0];
        EXPECT_EQ(value.subject_id, 7U);
        ASSERT_TRUE(opk::embedding::valid(value));
        EXPECT_EQ(value.space->id, "test:a");
        EXPECT_FLOAT_EQ(value.values[0], .6f);
        EXPECT_FLOAT_EQ(value.values[1], .8f);
    });
    for (const float value :
         {std::numeric_limits<float>::max(), std::numeric_limits<float>::denorm_min(), 0.0f}) {
        values[0] = values[1] = value;
        FrameResults output;
        ASSERT_TRUE(parser.parse(input, output));
        output.for_each<ObjectEmbeddingsT>([value](const auto &payload) {
            EXPECT_EQ(opk::embedding::valid(*payload.embeddings[0]), value != 0);
        });
    }
    values[0] = 3;
    values[1] = 4;
    const opk::AttributeMap unspecified;
    opk::TensorParser::Input anonymous(unspecified);
    anonymous.tensors[0] = &tensor;
    anonymous.inferenceInfo.modelName = "not-a-space-declaration";
    FrameResults unknown;
    ASSERT_TRUE(parser.parse(anonymous, unknown));
    unknown.for_each<ObjectEmbeddingsT>([](const auto &payload) {
        EXPECT_FALSE(opk::embedding::valid(*payload.embeddings[0]));
        EXPECT_TRUE(payload.embeddings[0]->space->id.empty());
        EXPECT_FALSE(payload.embeddings[0]->subject_id);
    });
    values[0] = std::numeric_limits<float>::infinity();
    FrameResults invalid;
    EXPECT_FALSE(parser.parse(input, invalid));
    EXPECT_EQ(invalid.size(), 0U);
    opk::TensorView truncated(
        values.data(), sizeof(float), opk::Shape(1, 2), opk::Dtype::Float32, 1, 0);
    input.tensors[0] = &truncated;
    EXPECT_FALSE(parser.parse(input, invalid));
    values[0] = 3;
    for (const auto shape :
         {opk::Shape(1, 0), opk::Shape(1, -1), opk::Shape(2, 2), opk::Shape(1, 3)}) {
        opk::TensorView malformed(
            values.data(), values.size() * sizeof(float), shape, opk::Dtype::Float32, 1, 0);
        input.tensors[0] = &malformed;
        EXPECT_FALSE(parser.parse(input, invalid));
    }
    EXPECT_EQ(invalid.size(), 0U);
}

uint64_t step(opk::tracker::Tracker &tracker,
              const opk::tracker::Config &config,
              const std::string &space,
              const uint64_t subject,
              const float x) {
    auto result = frame(subject, x);
    auto value = embedding(space);
    value.subject_id = subject;
    addEmbedding(result, value);
    tracker.process(result, config, subject * 100);
    uint64_t trackId = 0;
    result.for_each<ObjectTracksT>([&](const auto &tracks) {
        for (const auto &track : tracks.tracks)
            if (track->source_id == subject)
                trackId = track->identity->track_id;
    });
    return trackId;
}

TEST(EmbeddingContract, ActiveAndDormantHistoryKeepSpaceIdentity) {
    opk::tracker::Tracker tracker;
    opk::tracker::Config config;
    config.useKalman = false;
    config.minHitsToConfirm = 1;
    config.maxMissedFrames = 0;
    config.associationMode = opk::tracker::AssociationMode::Embedding;
    const auto first = step(tracker, config, "test:a", 7, 0);
    ASSERT_NE(first, 0U);
    EXPECT_EQ(step(tracker, config, "test:a", 8, 50), first);
    const auto different = step(tracker, config, "test:b", 9, 0);
    EXPECT_NE(different, first);
    EXPECT_EQ(step(tracker, config, "test:a", 10, 80), first);
    config.associationMode = opk::tracker::AssociationMode::Hybrid;
    EXPECT_EQ(step(tracker, config, "test:c", 11, 80), first);
    config.associationMode = opk::tracker::AssociationMode::Embedding;
    EXPECT_EQ(step(tracker, config, "test:c", 12, 0), first);
}
} // namespace
