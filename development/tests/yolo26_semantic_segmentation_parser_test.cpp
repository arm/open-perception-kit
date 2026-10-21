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
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "opk/AttributeMap.h"
#include "opk/FrameResults.h"
#include "opk/TensorParser.h"
#include "opk/TensorView.h"
#include "postproc/Yolo26SemanticSegmentationParser.h"

namespace {

using open_perception_kit::metadata::SemanticSegmentationMasksT;
constexpr std::size_t ModelSize = 1024U;
constexpr std::uint64_t SourceId = 1000000U;

class Yolo26SemanticSegmentationParserTest : public ::testing::Test {
  protected:
    Yolo26SemanticSegmentationParserTest()
        : classes(ModelSize * ModelSize, 0U), tensor(classes.data(),
                                                     classes.size(),
                                                     opk::Shape(1, 1024, 1024),
                                                     opk::Dtype::Uint8,
                                                     1.0F,
                                                     0.0F),
          input(attributes) {}

    void SetUp() override {
        input.tensors[0] = &tensor;
        input.inferenceInfo.modelName = "yolo26n-sem";
        input.inferenceInfo.inferElementId = "infer-sem";
        input.inferenceInfo.parentId = SourceId;
        input.inferenceInfo.image = {
            .width = 3U, .height = 2U, .modelWidth = ModelSize, .modelHeight = ModelSize};
        input.producerInfo.instance_id = "infer-sem/postprocessor";
        input.producerInfo.component = "opk-std-ops/GenericPostprocess";
        input.producerInfo.implementation = "Yolo26SemanticSegmentationParser";
        open_perception_kit::metadata::FrameContextT frame;
        frame.video = std::make_unique<open_perception_kit::metadata::VideoFrameContextT>();
        frame.video->object = open_perception_kit::makeObjectMeta(SourceId);
        frame.video->original_width = 3U;
        frame.video->original_height = 2U;
        output.add(std::move(frame));
    }

    opk::AttributeMap attributes;
    std::vector<std::uint8_t> classes;
    opk::TensorView tensor;
    opk::TensorParser::Input input;
    open_perception_kit::FrameResults output;
    opk::stdop::postproc::Yolo26SemanticSegmentationParser parser;
};

TEST_F(Yolo26SemanticSegmentationParserTest, PreservesBoundaryClassesAndSerializesTypedPayload) {
    for (std::size_t y = 0U; y < ModelSize; ++y) {
        for (std::size_t x = 0U; x < ModelSize; ++x) {
            classes[y * ModelSize + x] = (x / 256U + y / 512U) % 2U == 0U ? 0U : 18U;
        }
    }
    const auto result = parser.parse(input, output);
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    ASSERT_EQ(output.count<SemanticSegmentationMasksT>(), 1U);
    const auto original = output.get<SemanticSegmentationMasksT>();
    if (!original.has_value()) {
        ADD_FAILURE() << "missing SemanticSegmentationMasks payload";
        return;
    }
    ASSERT_EQ(original->value().masks.size(), 1U);
    ASSERT_NE(original->value().masks[0], nullptr);
    ASSERT_NE(original->value().masks[0]->bitmap, nullptr);
    // Ordinary nearest uses x=0,341,682 and y=0,512, without interpolating class IDs.
    const std::vector<std::uint8_t> expected{0U, 18U, 0U, 18U, 0U, 18U};
    EXPECT_EQ(original->value().masks[0]->bitmap->pixels, expected);

    const auto packet = open_perception_kit::serialize(output);
    const open_perception_kit::FrameResults decoded{std::span<const std::uint8_t>(packet)};
    ASSERT_TRUE(decoded.valid());
    EXPECT_EQ(decoded.producer_identity(),
              open_perception_kit::container::producer_identity_status::exact_match);
    EXPECT_EQ(decoded.count<open_perception_kit::metadata::FrameContextT>(), 1U);
    ASSERT_EQ(decoded.count<SemanticSegmentationMasksT>(), 1U);
    EXPECT_EQ(decoded.count<open_perception_kit::metadata::InstanceSegmentationMasksT>(), 0U);
    const auto restored = decoded.get<SemanticSegmentationMasksT>();
    if (!restored.has_value()) {
        ADD_FAILURE() << "missing decoded SemanticSegmentationMasks payload";
        return;
    }
    const auto &payload = restored->value();
    ASSERT_NE(payload.layer, nullptr);
    EXPECT_EQ(payload.layer->content_type, "semanticSegmentation");
    EXPECT_EQ(payload.layer->label_family, "cityscapes");
    EXPECT_EQ(payload.layer->model, "yolo26n-sem");
    EXPECT_EQ(payload.layer->infer_element_id, "infer-sem");
    ASSERT_NE(payload.layer->producer, nullptr);
    EXPECT_EQ(payload.layer->producer->instance_id, "infer-sem/postprocessor");
    EXPECT_EQ(payload.layer->producer->implementation, "Yolo26SemanticSegmentationParser");
    ASSERT_EQ(payload.masks.size(), 1U);
    ASSERT_NE(payload.masks[0], nullptr);
    ASSERT_NE(payload.masks[0]->object, nullptr);
    EXPECT_EQ(payload.masks[0]->object->parent_id, SourceId);
    ASSERT_NE(payload.masks[0]->bitmap, nullptr);
    const auto &bitmap = *payload.masks[0]->bitmap;
    EXPECT_EQ(bitmap.value_type, "Uint8");
    EXPECT_EQ(bitmap.width, 3U);
    EXPECT_EQ(bitmap.height, 2U);
    EXPECT_EQ(bitmap.pixels, expected);
}

TEST_F(Yolo26SemanticSegmentationParserTest, RejectsClass19WithoutChangingFrameResults) {
    const auto before = open_perception_kit::serialize(output);
    // The final model-grid pixel is never sampled by the 3x2 output; validate it anyway.
    for (const auto position : {std::size_t{0U}, classes.size() - 1U}) {
        classes[position] = 19U;
        const auto result = parser.parse(input, output);
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().flag, opk::ErrorFlag::InvalidData);
        EXPECT_EQ(output.count<SemanticSegmentationMasksT>(), 0U);
        EXPECT_EQ(output.count<open_perception_kit::metadata::FrameContextT>(), 1U);
        EXPECT_EQ(open_perception_kit::serialize(output), before);
        classes[position] = 0U;
    }
}

} // namespace
