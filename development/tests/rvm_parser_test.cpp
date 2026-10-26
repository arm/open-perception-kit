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
#include <limits>
#include <vector>

#include "opk/TensorParser.h"
#include "postproc/RvmParser.h"

TEST(RvmParser, PreservesGradedBackgroundOpacityAndProvenance) {
    const std::array<float, 8> values = {-0.1f, 0.0f, 0.25f, 0.5f, 0.75f, 0.99f, 1.0f, 1.1f};
    opk::TensorView alpha(
        values.data(), sizeof(values), opk::Shape(1, 1, 2, 4), opk::Dtype::Float32, 1.0f, 0.0f);
    const opk::AttributeMap attributes;
    opk::TensorParser::Input input(attributes);
    input.tensors[0] = &alpha; // Foreground RGB is deliberately unused by this parser.
    input.tensors[1] = &alpha;
    input.inferenceInfo.parentId = 42;
    input.inferenceInfo.modelName = "rvm-test";
    input.producerInfo.instance_id = "rvm-parser";
    open_perception_kit::FrameResults output;
    ASSERT_TRUE(opk::stdop::postproc::RvmParser().parse(input, output));
    const auto payload = output.get<open_perception_kit::metadata::SegmentationMasksT>();
    if (!payload) {
        ADD_FAILURE() << "Missing SegmentationMasks payload";
        return;
    }
    const auto &masks = payload.value().value();
    ASSERT_EQ(masks.masks.size(), 1U);
    EXPECT_EQ(masks.layer->compositing_mode, "backgroundReplacement");
    EXPECT_EQ(masks.layer->model, "rvm-test");
    EXPECT_EQ(masks.layer->producer->instance_id, "rvm-parser");
    EXPECT_EQ(masks.masks[0]->object->parent_id, 42U);
    const auto &bitmap = *masks.masks[0]->bitmap;
    EXPECT_EQ(bitmap.width, 4U);
    EXPECT_EQ(bitmap.height, 2U);
    EXPECT_EQ(bitmap.pixels, (std::vector<uint8_t>{255, 255, 191, 128, 64, 3, 0, 0}));
}

TEST(RvmParser, RejectsInvalidAlphaWithoutPublishingAPartialMask) {
    const opk::AttributeMap attributes;
    opk::TensorParser::Input input(attributes);
    open_perception_kit::FrameResults output;
    opk::stdop::postproc::RvmParser parser;
    EXPECT_FALSE(parser.parse(input, output));
    float value = 0.5f;
    for (const auto shape : {opk::Shape(1, 2),
                             opk::Shape(2, 1, 1, 1),
                             opk::Shape(1, 2, 1, 1),
                             opk::Shape(1, 1, -1, 1),
                             opk::Shape(1, 1, 0, 1),
                             opk::Shape(1, 1, 2, 2)}) {
        opk::TensorView alpha(&value, sizeof(value), shape, opk::Dtype::Float32, 1.0f, 0.0f);
        input.tensors[0] = input.tensors[1] = &alpha;
        EXPECT_FALSE(parser.parse(input, output));
    }
    opk::TensorView alpha(
        &value, sizeof(value), opk::Shape(1, 1, 1, 1), opk::Dtype::Float32, 1.0f, 0.0f);
    input.tensors[0] = input.tensors[1] = &alpha;
    for (const auto invalid : {std::numeric_limits<float>::quiet_NaN(),
                               std::numeric_limits<float>::infinity(),
                               -std::numeric_limits<float>::infinity()}) {
        value = invalid;
        EXPECT_FALSE(parser.parse(input, output));
    }
    EXPECT_FALSE(output.get<open_perception_kit::metadata::SegmentationMasksT>());
}
