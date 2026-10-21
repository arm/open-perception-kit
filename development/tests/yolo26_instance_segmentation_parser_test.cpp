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
#include "postproc/Yolo26InstanceSegmentationParser.h"

namespace {

using Masks = open_perception_kit::metadata::InstanceSegmentationMasksT;
constexpr std::uint64_t ParentId = 1000000U;

struct ParserFixture {
    std::vector<float> detections = std::vector<float>(300U * 38U, 0.0F);
    std::vector<float> prototypes = std::vector<float>(32U * 160U * 160U, 0.0F);
    opk::TensorView detectionTensor{detections.data(),
                                    detections.size() * sizeof(float),
                                    opk::Shape(1, 300, 38),
                                    opk::Dtype::Float32,
                                    1.0F,
                                    0.0F};
    opk::TensorView prototypeTensor{prototypes.data(),
                                    prototypes.size() * sizeof(float),
                                    opk::Shape(1, 32, 160, 160),
                                    opk::Dtype::Float32,
                                    1.0F,
                                    0.0F};
    opk::AttributeMap attributes;
    opk::TensorParser::Input input{attributes};
    open_perception_kit::FrameResults output;
    opk::stdop::postproc::Yolo26InstanceSegmentationParser parser;

    ParserFixture(std::size_t width = 640U,
                  std::size_t height = 640U,
                  std::size_t left = 0U,
                  std::size_t right = 0U,
                  std::size_t top = 0U,
                  std::size_t bottom = 0U) {
        input.tensors[0] = &detectionTensor;
        input.tensors[1] = &prototypeTensor;
        input.inferenceInfo.parentId = ParentId;
        input.inferenceInfo.modelName = "instance-regression";
        input.inferenceInfo.inferElementId = "segmentation-infer";
        input.inferenceInfo.image = {.width = width,
                                     .height = height,
                                     .modelWidth = 640U,
                                     .modelHeight = 640U,
                                     .letterboxLeft = left,
                                     .letterboxRight = right,
                                     .letterboxTop = top,
                                     .letterboxBottom = bottom};
        input.producerInfo.instance_id = "segmentation-postprocess";
        input.producerInfo.component = "GenericPostprocess";
        input.producerInfo.implementation = "Yolo26InstanceSegmentationParser";

        open_perception_kit::metadata::FrameContextT context;
        context.video = std::make_unique<open_perception_kit::metadata::VideoFrameContextT>();
        context.video->object = open_perception_kit::makeObjectMeta(ParentId);
        context.video->original_width = width;
        context.video->original_height = height;
        output.add(std::move(context));
        detect(0U);
    }

    void detect(std::size_t row,
                float left = 0.0F,
                float top = 0.0F,
                float right = 640.0F,
                float bottom = 640.0F) {
        const auto base = row * 38U;
        detections[base] = left;
        detections[base + 1U] = top;
        detections[base + 2U] = right;
        detections[base + 3U] = bottom;
        detections[base + 4U] = 0.9F;
        detections[base + 5U] = 2.0F;
        detections[base + 6U] = 1.0F;
    }

    const Masks &masks() const {
        return output.get<Masks>().value().value();
    }
};

struct GradientCase {
    std::size_t width;
    std::size_t height;
    std::size_t leadingPadding;
    std::size_t trailingPadding;
    bool horizontal;
    std::size_t firstForeground;
};

class InstanceMaskGeometry : public testing::TestWithParam<GradientCase> {};

TEST_P(InstanceMaskGeometry, InvertsRecordedLetterboxTransform) {
    const auto geometry = GetParam();
    ParserFixture fixture(geometry.width,
                          geometry.height,
                          geometry.horizontal ? geometry.leadingPadding : 0U,
                          geometry.horizontal ? geometry.trailingPadding : 0U,
                          geometry.horizontal ? 0U : geometry.leadingPadding,
                          geometry.horizontal ? 0U : geometry.trailingPadding);
    for (std::size_t y = 0U; y < 160U; ++y) {
        for (std::size_t x = 0U; x < 160U; ++x) {
            fixture.prototypes[y * 160U + x] =
                static_cast<float>(geometry.horizontal ? x : y) - 100.0F;
        }
    }

    const auto parsed = fixture.parser.parse(fixture.input, fixture.output);
    ASSERT_TRUE(parsed.has_value()) << parsed.error().toString();
    ASSERT_EQ(fixture.output.count<Masks>(), 1U);
    ASSERT_EQ(fixture.masks().masks.size(), 1U);
    const auto &mask = *fixture.masks().masks.front();
    ASSERT_NE(mask.bitmap, nullptr);
    EXPECT_EQ(mask.bitmap->width, geometry.width);
    EXPECT_EQ(mask.bitmap->height, geometry.height);
    std::vector<std::uint8_t> expected(geometry.width * geometry.height);
    for (std::size_t y = 0U; y < geometry.height; ++y) {
        for (std::size_t x = 0U; x < geometry.width; ++x) {
            expected[y * geometry.width + x] =
                (geometry.horizontal ? x : y) >= geometry.firstForeground ? 1U : 0U;
        }
    }
    EXPECT_EQ(mask.bitmap->pixels, expected);
}

// Fixed boundaries come from the recorded preprocessing transform, independent of parser helpers.
INSTANTIATE_TEST_SUITE_P(RecordedPadding,
                         InstanceMaskGeometry,
                         testing::Values(GradientCase{1003U, 810U, 61U, 62U, false, 534U},
                                         GradientCase{810U, 1003U, 61U, 62U, true, 534U},
                                         GradientCase{640U, 640U, 0U, 0U, false, 402U},
                                         GradientCase{1003U, 1000U, 1U, 1U, false, 629U}));

TEST(Yolo26InstanceSegmentationParser, ClampsInterpolationAtPrototypeEdges) {
    ParserFixture fixture;
    std::fill_n(fixture.prototypes.begin(), 160U * 160U, -1.0F);
    std::fill_n(fixture.prototypes.begin(), 160U, 1.0F);
    std::fill_n(fixture.prototypes.begin() + 159U * 160U, 160U, 1.0F);
    const auto parsed = fixture.parser.parse(fixture.input, fixture.output);
    ASSERT_TRUE(parsed.has_value()) << parsed.error().toString();
    ASSERT_EQ(fixture.output.count<Masks>(), 1U);
    ASSERT_EQ(fixture.masks().masks.size(), 1U);
    std::vector<std::uint8_t> expected(640U * 640U);
    std::fill_n(expected.begin(), 4U * 640U, 1U);
    std::fill_n(expected.begin() + 636U * 640U, 4U * 640U, 1U);
    ASSERT_NE(fixture.masks().masks.front()->bitmap, nullptr);
    EXPECT_EQ(fixture.masks().masks.front()->bitmap->pixels, expected);
}

TEST(Yolo26InstanceSegmentationParser, FractionalBoxClippingIsIndependentOfCandidateCount) {
    std::vector<std::uint8_t> firstBitmap;
    std::vector<std::uint8_t> expected(640U * 640U);
    for (std::size_t y = 11U; y <= 20U; ++y) {
        for (std::size_t x = 11U; x <= 20U; ++x) {
            expected[y * 640U + x] = 1U;
        }
    }
    for (const std::size_t count : {1U, 49U, 50U}) {
        SCOPED_TRACE(count);
        ParserFixture fixture;
        std::fill_n(fixture.prototypes.begin(), 160U * 160U, 1.0F);
        for (std::size_t row = 0U; row < count; ++row) {
            fixture.detect(row, 10.25F, 10.25F, 20.25F, 20.25F);
        }
        const auto parsed = fixture.parser.parse(fixture.input, fixture.output);
        ASSERT_TRUE(parsed.has_value()) << parsed.error().toString();
        ASSERT_EQ(fixture.output.count<Masks>(), 1U);
        ASSERT_EQ(fixture.masks().masks.size(), count);
        const auto &mask = *fixture.masks().masks.front();
        ASSERT_NE(mask.bitmap, nullptr);
        ASSERT_NE(mask.box, nullptr);
        EXPECT_FLOAT_EQ(mask.box->x, 10.25F);
        EXPECT_FLOAT_EQ(mask.box->y, 10.25F);
        EXPECT_FLOAT_EQ(mask.box->width, 10.0F);
        EXPECT_FLOAT_EQ(mask.box->height, 10.0F);
        EXPECT_EQ(mask.bitmap->pixels, expected);
        if (firstBitmap.empty()) {
            firstBitmap = mask.bitmap->pixels;
        } else {
            EXPECT_EQ(mask.bitmap->pixels, firstBitmap);
        }
    }
}

TEST(Yolo26InstanceSegmentationParser, SerializesMaskPixelsAndMetadata) {
    ParserFixture fixture(4U, 3U);
    std::fill_n(fixture.prototypes.begin(), 160U * 160U, 1.0F);
    const auto parsed = fixture.parser.parse(fixture.input, fixture.output);
    ASSERT_TRUE(parsed.has_value()) << parsed.error().toString();
    const auto packet = open_perception_kit::serialize(fixture.output);
    const open_perception_kit::FrameResults decoded{std::span<const std::uint8_t>(packet)};
    ASSERT_TRUE(decoded.valid()) << decoded.error();
    EXPECT_EQ(decoded.size(), 2U);
    ASSERT_EQ(decoded.count<open_perception_kit::metadata::FrameContextT>(), 1U);
    ASSERT_EQ(decoded.count<Masks>(), 1U);
    const auto payload = decoded.get<Masks>();
    ASSERT_TRUE(payload.has_value());
    const auto &masks = payload->value();
    ASSERT_NE(masks.layer, nullptr);
    EXPECT_EQ(masks.layer->content_type, "instanceSegmentation");
    EXPECT_EQ(masks.layer->label_family, "coco");
    EXPECT_EQ(masks.layer->model, fixture.input.inferenceInfo.modelName);
    EXPECT_EQ(masks.layer->infer_element_id, fixture.input.inferenceInfo.inferElementId);
    ASSERT_NE(masks.layer->producer, nullptr);
    EXPECT_EQ(masks.layer->producer->instance_id, fixture.input.producerInfo.instance_id);
    EXPECT_EQ(masks.layer->producer->component, fixture.input.producerInfo.component);
    EXPECT_EQ(masks.layer->producer->implementation, fixture.input.producerInfo.implementation);
    ASSERT_EQ(masks.masks.size(), 1U);
    const auto &mask = *masks.masks.front();
    ASSERT_NE(mask.object, nullptr);
    EXPECT_NE(mask.object->id, 0U);
    EXPECT_EQ(mask.object->parent_id, ParentId);
    EXPECT_EQ(mask.object->id, fixture.masks().masks.front()->object->id);
    EXPECT_EQ(mask.object->creation_ts_ns, fixture.masks().masks.front()->object->creation_ts_ns);
    const auto parent = decoded.get<open_perception_kit::metadata::FrameContextT>();
    ASSERT_TRUE(parent.has_value());
    ASSERT_NE(parent->value().video, nullptr);
    ASSERT_NE(parent->value().video->object, nullptr);
    EXPECT_EQ(parent->value().video->object->id, mask.object->parent_id);
    EXPECT_EQ(parent->value().video->original_width, 4U);
    EXPECT_EQ(parent->value().video->original_height, 3U);
    EXPECT_EQ(mask.class_id, 2);
    EXPECT_FLOAT_EQ(mask.confidence, 0.9F);
    ASSERT_NE(mask.box, nullptr);
    EXPECT_FLOAT_EQ(mask.box->x, 0.0F);
    EXPECT_FLOAT_EQ(mask.box->y, 0.0F);
    EXPECT_FLOAT_EQ(mask.box->width, 4.0F);
    EXPECT_FLOAT_EQ(mask.box->height, 3.0F);
    ASSERT_NE(mask.bitmap, nullptr);
    EXPECT_EQ(mask.bitmap->width, 4U);
    EXPECT_EQ(mask.bitmap->height, 3U);
    EXPECT_EQ(mask.bitmap->value_type, "Uint8");
    EXPECT_EQ(mask.bitmap->pixels, std::vector<std::uint8_t>(12U, 1U));
}

} // namespace
