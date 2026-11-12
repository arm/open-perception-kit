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

#include "opk/FrameResults.h"
#include "postproc/ModNetSegmentationParser.h"
#include "postproc/PaddleocrParser.h"
#include <array>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>

namespace {
using namespace open_perception_kit;
using namespace open_perception_kit::metadata;
const opk::ImageInferenceMetadata Image{.width = 4,
                                        .height = 4,
                                        .modelWidth = 4,
                                        .modelHeight = 4,
                                        .frameId = 7,
                                        .frameWidth = 4,
                                        .frameHeight = 4};

TEST(MaskMetadata, EncodingMeaningAndExactStorageAreValidated) {
    opk::Bitmap bitmap(opk::Bitmap::Type::Uint8, 2, 1);
    bitmap.set8(1, 0, 128);
    SegmentationMaskT mask;
    mask.bitmap = makeBitmapData(bitmap, Image);
    EXPECT_FALSE(validMask(mask));
    mask.meaning = OpacityMeaning::BackgroundOpacity;
    EXPECT_TRUE(validMask(mask));
    mask.meaning = OpacityMeaning::RegionOpacity;
    EXPECT_TRUE(validMask(mask));
    mask.bitmap->pixels.push_back(0);
    EXPECT_FALSE(validMask(mask));
    mask.bitmap->pixels.pop_back();
    mask.bitmap->width = std::numeric_limits<uint32_t>::max();
    EXPECT_FALSE(validMask(mask));
    mask.bitmap->width = 2;
    mask.bitmap->encoding = static_cast<BitmapEncoding>(99);
    EXPECT_FALSE(validMask(mask));
    mask.bitmap->encoding = BitmapEncoding::Uint8;
    mask.bitmap->space.reset();
    EXPECT_FALSE(validMask(mask));
}

TEST(MaskMetadata, CategoryZeroVoidAndLittleEndianSamplesRoundTrip) {
    opk::Bitmap bitmap(opk::Bitmap::Type::Uint32, 2, 1);
    const std::array<uint32_t, 2> values{0, 0x12345678};
    std::memcpy(bitmap.getMutableData(), values.data(), sizeof(values));
    auto mask = std::make_unique<SemanticSegmentationMaskT>();
    mask->bitmap = makeBitmapData(bitmap, Image);
    ASSERT_TRUE(mask->bitmap);
    EXPECT_EQ(mask->bitmap->pixels, (std::vector<uint8_t>{0, 0, 0, 0, 0x78, 0x56, 0x34, 0x12}));
    EXPECT_FALSE(validMask(*mask));
    mask->label_set = "test:labels";
    EXPECT_TRUE(validMask(*mask));
    EXPECT_FALSE(mask->void_value);
    mask->void_value = 0;
    SemanticSegmentationMasksT payload;
    payload.masks.push_back(std::move(mask));
    FrameResults frame;
    frame.add(std::move(payload));
    const FrameResults decoded(serialize(frame));
    decoded.for_each<SemanticSegmentationMasksT>([](const auto &result) {
        ASSERT_EQ(result.masks.size(), 1U);
        EXPECT_EQ(result.masks[0]->void_value, 0U);
        EXPECT_TRUE(validMask(*result.masks[0]));
    });
    ASSERT_EQ(decoded.count<SemanticSegmentationMasksT>(), 1U);
}

TEST(MaskMetadata, BinaryMembershipAndOptionalInstanceFieldsAreDistinct) {
    opk::Bitmap bitmap(opk::Bitmap::Type::Uint8, 2, 1);
    bitmap.set8(1, 0, 1);
    InstanceSegmentationMaskT mask;
    mask.bitmap = makeBitmapData(bitmap, Image);
    EXPECT_TRUE(validMask(mask));
    mask.class_id = 0;
    EXPECT_FALSE(validMask(mask));
    mask.label_set = "test:labels";
    mask.confidence = 0;
    EXPECT_TRUE(validMask(mask));
    mask.bitmap->pixels[1] = 255;
    EXPECT_FALSE(validMask(mask));
    mask.bitmap->pixels[1] = 1;
    mask.confidence = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(validMask(mask));
    mask.confidence = 1;
    mask.box = makeImageBoundingBox(Image, 0, 0, 1, 1);
    EXPECT_TRUE(validMask(mask));
    mask.box->space->frame_id = 8;
    EXPECT_FALSE(validMask(mask));
}

TEST(MaskMetadata, ModelPaddingIsRemovedAndCropPlacementIsSelfContained) {
    opk::Bitmap bitmap(opk::Bitmap::Type::Uint8, 4, 4);
    for (size_t y = 0; y < 4; ++y)
        for (size_t x = 0; x < 4; ++x)
            bitmap.set8(x, y, static_cast<uint8_t>(y * 4 + x));
    auto image = Image;
    image.letterboxLeft = image.letterboxRight = image.letterboxTop = image.letterboxBottom = 1;
    image.frameWidth = image.frameHeight = 8;
    image.cropX = image.cropY = 2;
    const auto data = makeModelMaskBitmapData(bitmap, image);
    ASSERT_TRUE(data);
    EXPECT_EQ(data->pixels, (std::vector<uint8_t>{5, 6, 9, 10}));
    EXPECT_EQ(data->width, 2U);
    const auto projected = projectBitmap(*data, 80, 80);
    ASSERT_TRUE(projected);
    EXPECT_EQ(projected.value_or(std::array<double, 4>{}), (std::array<double, 4>{20, 20, 40, 40}));
    image.letterboxLeft = 4;
    EXPECT_FALSE(makeModelMaskBitmapData(bitmap, image));
}

TEST(MaskMetadata, OpacityParsersValidateTensorsBeforePublishing) {
    opk::stdop::postproc::ModNetSegmentationParser modnet;
    opk::stdop::postproc::PaddleOcrDetectionParser ocr;
    for (auto *const parser : std::array<opk::TensorParser *, 2>{&modnet, &ocr}) {
        std::array<float, 4> values{0, .5, 1, 2};
        opk::TensorView tensor(
            values.data(), sizeof(values), opk::Shape(1, 1, 2, 2), opk::Dtype::Float32, 1, 0);
        opk::AttributeMap attributes;
        opk::TensorParser::Input input(attributes);
        input.tensors[0] = &tensor;
        input.inferenceInfo.image = Image;
        input.inferenceInfo.parentId = 999;
        FrameResults output;
        ASSERT_TRUE(parser->parse(input, output));
        output.for_each<SegmentationMasksT>([&](const auto &payload) {
            ASSERT_EQ(payload.masks.size(), 1U);
            EXPECT_TRUE(validMask(*payload.masks[0]));
            EXPECT_EQ(payload.masks[0]->bitmap->space->frame_id, 7U);
            EXPECT_EQ(payload.masks[0]->meaning,
                      parser == &modnet ? OpacityMeaning::BackgroundOpacity
                                        : OpacityMeaning::RegionOpacity);
        });
        for (const float invalid :
             {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
            values[0] = invalid;
            FrameResults rejected;
            EXPECT_FALSE(parser->parse(input, rejected));
            EXPECT_EQ(rejected.size(), 0U);
        }
        values[0] = 0;
        attributes.set("thresholdHigh", .1);
        FrameResults rejected;
        EXPECT_FALSE(parser->parse(input, rejected));
    }
}
} // namespace
