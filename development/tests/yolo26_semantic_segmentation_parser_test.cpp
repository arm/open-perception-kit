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
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
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

// Golden class maps follow Ultralytics 8.4.80's class_map_input / scale_masks path
// with mode="nearest", using PyTorch's ordinary nearest (floor-scaled indices).
// Expected vectors are hand-derived, independently of the parser's sampling helper.
class Yolo26SemanticSegmentationParser : public ::testing::Test {
  protected:
    Yolo26SemanticSegmentationParser() : classes(ModelSize * ModelSize, 0U), input(attributes) {}

    void SetUp() override {
        bindTensor();
        input.inferenceInfo.modelName = "yolo26n-sem";
        input.inferenceInfo.inferElementId = "infer-sem";
        input.inferenceInfo.image.modelWidth = ModelSize;
        input.inferenceInfo.image.modelHeight = ModelSize;
        input.producerInfo.instance_id = "infer-sem/postprocessor";
        input.producerInfo.component = "opk-std-ops/GenericPostprocess";
        input.producerInfo.implementation = "Yolo26SemanticSegmentationParser";
        setSourceSize(3U, 2U);
    }

    void bindTensor(float scale = 1.0F, float zeroPoint = 0.0F) {
        tensor = opk::TensorView(classes.data(),
                                 classes.size(),
                                 opk::Shape(1, 1024, 1024),
                                 opk::Dtype::Uint8,
                                 scale,
                                 zeroPoint);
        input.tensors[0] = &tensor;
    }

    void setSourceSize(std::size_t width, std::size_t height) {
        input.inferenceInfo.parentId = SourceId;
        input.inferenceInfo.image.width = width;
        input.inferenceInfo.image.height = height;
        output = open_perception_kit::FrameResults{};
        open_perception_kit::metadata::FrameContextT frame;
        frame.layer = open_perception_kit::makeLayerInfo({.contentType = "frameContext"});
        frame.video = std::make_unique<open_perception_kit::metadata::VideoFrameContextT>();
        frame.video->object = open_perception_kit::makeObjectMeta(SourceId);
        frame.video->original_width = width;
        frame.video->original_height = height;
        output.add(std::move(frame));
    }

    void fillClassGrid() {
        for (std::size_t y = 0U; y < ModelSize; ++y) {
            for (std::size_t x = 0U; x < ModelSize; ++x) {
                classes[y * ModelSize + x] = static_cast<std::uint8_t>(4U * (y / 256U) + x / 256U);
            }
        }
    }

    opk::Result<void> parse() {
        return parser.parse(input, output);
    }

    const SemanticSegmentationMasksT *payload() const {
        const auto ref = output.get<SemanticSegmentationMasksT>();
        return ref ? &ref->value() : nullptr;
    }

    void expectPixels(const std::vector<std::uint8_t> &expected) const {
        ASSERT_NE(payload(), nullptr);
        ASSERT_EQ(payload()->masks.size(), 1U);
        ASSERT_NE(payload()->masks[0], nullptr);
        ASSERT_NE(payload()->masks[0]->bitmap, nullptr);
        EXPECT_EQ(payload()->masks[0]->bitmap->pixels, expected);
    }

    void expectRejectedWithoutAppend() {
        const auto before = output.size();
        const auto masksBefore = output.count<SemanticSegmentationMasksT>();
        const auto result = parse();
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().flag, opk::ErrorFlag::InvalidData);
        EXPECT_FALSE(result.error().info.empty());
        EXPECT_EQ(output.size(), before);
        EXPECT_EQ(output.count<SemanticSegmentationMasksT>(), masksBefore);
    }

    opk::AttributeMap attributes;
    std::vector<std::uint8_t> classes;
    opk::TensorView tensor;
    opk::TensorParser::Input input;
    open_perception_kit::FrameResults output;
    opk::stdop::postproc::Yolo26SemanticSegmentationParser parser;
};

TEST_F(Yolo26SemanticSegmentationParser, UsesOrdinaryNearestAndPreservesSemanticMetadata) {
    fillClassGrid();
    const auto result = parse();
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    // Ordinary nearest samples 1024->3 at x=0,341,682 and 1024->2 at y=0,512.
    // nearest-exact would sample x=170,512,853 and produce different class IDs.
    expectPixels({0U, 1U, 2U, 8U, 9U, 10U});
    ASSERT_NE(payload(), nullptr);
    EXPECT_EQ(output.count<SemanticSegmentationMasksT>(), 1U);
    EXPECT_EQ(output.count<open_perception_kit::metadata::InstanceSegmentationMasksT>(), 0U);
    EXPECT_EQ(output.count<open_perception_kit::metadata::SegmentationMasksT>(), 0U);
    const auto &layer = payload()->layer;
    ASSERT_NE(layer, nullptr);
    EXPECT_EQ(layer->content_type, "semanticSegmentation");
    EXPECT_EQ(layer->label_family, "cityscapes");
    EXPECT_EQ(layer->model, "yolo26n-sem");
    EXPECT_EQ(layer->infer_element_id, "infer-sem");
    EXPECT_TRUE(layer->compositing_mode.empty());
    ASSERT_NE(layer->producer, nullptr);
    EXPECT_EQ(layer->producer->instance_id, "infer-sem/postprocessor");
    EXPECT_EQ(layer->producer->component, "opk-std-ops/GenericPostprocess");
    EXPECT_EQ(layer->producer->implementation, "Yolo26SemanticSegmentationParser");
    const auto &mask = payload()->masks[0];
    ASSERT_NE(mask->object, nullptr);
    EXPECT_NE(mask->object->id, 0U);
    EXPECT_EQ(mask->object->parent_id, SourceId);
    EXPECT_EQ(mask->bitmap->width, 3U);
    EXPECT_EQ(mask->bitmap->height, 2U);
    EXPECT_EQ(mask->bitmap->value_type, "Uint8");
    EXPECT_EQ(parser.getProvidedContentTypes(),
              std::vector<std::string_view>{"semanticSegmentation"});
}

TEST_F(Yolo26SemanticSegmentationParser, CropsRecordedOddAsymmetricPaddingBeforeResizing) {
    std::fill(classes.begin(), classes.end(), 18U);
    auto &image = input.inferenceInfo.image;
    image.letterboxLeft = 509U;
    image.letterboxRight = 510U;
    image.letterboxTop = 510U;
    image.letterboxBottom = 511U;
    for (std::size_t y = 0U; y < 3U; ++y) {
        for (std::size_t x = 0U; x < 5U; ++x) {
            classes[(510U + y) * ModelSize + 509U + x] = static_cast<std::uint8_t>(5U * y + x);
        }
    }
    const auto result = parse();
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    // The active 5x3 region resized to 3x2 samples x=0,1,3 and y=0,1.
    expectPixels({0U, 1U, 3U, 5U, 6U, 8U});
}

TEST_F(Yolo26SemanticSegmentationParser, PreservesEveryClassPixelWithoutResizing) {
    setSourceSize(ModelSize, ModelSize);
    fillClassGrid();
    classes.front() = 18U;
    classes.back() = 17U;
    const auto result = parse();
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    expectPixels(classes);
    EXPECT_EQ(payload()->masks[0]->bitmap->width, ModelSize);
    EXPECT_EQ(payload()->masks[0]->bitmap->height, ModelSize);
}

TEST_F(Yolo26SemanticSegmentationParser, UpsamplesClassIdsWithOrdinaryNearest) {
    setSourceSize(5U, 4U);
    auto &image = input.inferenceInfo.image;
    image.letterboxLeft = 510U;
    image.letterboxRight = 511U;
    image.letterboxTop = 511U;
    image.letterboxBottom = 511U;
    std::fill(classes.begin(), classes.end(), 16U);
    const std::array<std::uint8_t, 6U> active = {0U, 1U, 18U, 11U, 13U, 17U};
    std::copy_n(active.begin(), 3U, classes.begin() + 511U * ModelSize + 510U);
    std::copy_n(active.begin() + 3U, 3U, classes.begin() + 512U * ModelSize + 510U);
    const auto result = parse();
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    // 3x2->5x4 uses x=0,0,1,1,2 and y=0,0,1,1; class IDs remain categorical.
    expectPixels({0U,  0U,  1U,  1U,  18U, 0U,  0U,  1U,  1U,  18U,
                  11U, 11U, 13U, 13U, 17U, 11U, 11U, 13U, 13U, 17U});
}

TEST_F(Yolo26SemanticSegmentationParser, RemovesLandscapeLetterboxingAndKeepsClassZero) {
    setSourceSize(4U, 1U);
    input.inferenceInfo.image.letterboxTop = 384U;
    input.inferenceInfo.image.letterboxBottom = 384U;
    std::fill(classes.begin(), classes.end(), 18U);
    for (std::size_t y = 384U; y < 640U; ++y) {
        for (std::size_t x = 0U; x < ModelSize; ++x) {
            classes[y * ModelSize + x] = static_cast<std::uint8_t>(x / 256U);
        }
    }
    const auto result = parse();
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    expectPixels({0U, 1U, 2U, 3U});
}

TEST_F(Yolo26SemanticSegmentationParser, RemovesPortraitLetterboxing) {
    setSourceSize(1U, 4U);
    input.inferenceInfo.image.letterboxLeft = 384U;
    input.inferenceInfo.image.letterboxRight = 384U;
    std::fill(classes.begin(), classes.end(), 18U);
    for (std::size_t y = 0U; y < ModelSize; ++y) {
        for (std::size_t x = 384U; x < 640U; ++x) {
            classes[y * ModelSize + x] = static_cast<std::uint8_t>(y / 256U);
        }
    }
    const auto result = parse();
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    expectPixels({0U, 1U, 2U, 3U});
}

TEST_F(Yolo26SemanticSegmentationParser, ReadsClassIdsThroughTensorConversion) {
    std::fill(classes.begin(), classes.end(), 136U);
    classes[0U] = 100U;
    bindTensor(0.5F, 100.0F);
    const auto result = parse();
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    expectPixels({0U, 18U, 18U, 18U, 18U, 18U});
}

TEST_F(Yolo26SemanticSegmentationParser, RejectsInvalidClassIdsAfterTensorConversion) {
    std::fill(classes.begin(), classes.end(), 1U);
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto infinity = std::numeric_limits<float>::infinity();
    const std::array<std::array<float, 2U>, 6U> invalidConversions = {{
        {0.5F, 0.0F},
        {-1.0F, 0.0F},
        {19.0F, 0.0F},
        {nan, 0.0F},
        {infinity, 0.0F},
        {1.0F, infinity},
    }};
    for (const auto &[scale, zeroPoint] : invalidConversions) {
        bindTensor(scale, zeroPoint);
        expectRejectedWithoutAppend();
    }
}

TEST_F(Yolo26SemanticSegmentationParser, PreservesAUniformClassMapIncludingHighestClass) {
    std::fill(classes.begin(), classes.end(), 18U);
    const auto result = parse();
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    expectPixels({18U, 18U, 18U, 18U, 18U, 18U});
}

TEST_F(Yolo26SemanticSegmentationParser, RejectsMissingOrTruncatedTensorBacking) {
    input.tensors[0] = nullptr;
    expectRejectedWithoutAppend();
    tensor = opk::TensorView(
        nullptr, classes.size(), opk::Shape(1, 1024, 1024), opk::Dtype::Uint8, 1.0F, 0.0F);
    input.tensors[0] = &tensor;
    expectRejectedWithoutAppend();
    tensor = opk::TensorView(classes.data(),
                             classes.size() - 1U,
                             opk::Shape(1, 1024, 1024),
                             opk::Dtype::Uint8,
                             1.0F,
                             0.0F);
    expectRejectedWithoutAppend();
}

TEST_F(Yolo26SemanticSegmentationParser, RejectsShapesOutsideThePinnedExportContract) {
    const std::array invalidShapes = {opk::Shape(1024, 1024),
                                      opk::Shape(1, 1, 1024, 1024),
                                      opk::Shape(2, 1024, 1024),
                                      opk::Shape(1, 1023, 1024),
                                      opk::Shape(1, 1024, 1023)};
    for (const auto &shape : invalidShapes) {
        tensor =
            opk::TensorView(classes.data(), classes.size(), shape, opk::Dtype::Uint8, 1.0F, 0.0F);
        expectRejectedWithoutAppend();
    }
}

TEST_F(Yolo26SemanticSegmentationParser, RejectsOutputTypesOtherThanUint8) {
    for (const auto type : {opk::Dtype::Int8, opk::Dtype::Float32, opk::Dtype::Int64}) {
        tensor = opk::TensorView(
            classes.data(), classes.size(), opk::Shape(1, 1024, 1024), type, 1.0F, 0.0F);
        expectRejectedWithoutAppend();
    }
}

TEST_F(Yolo26SemanticSegmentationParser, RejectsInvalidClassIdsEvenOutsideSampledPixels) {
    for (const auto classId : {19U, 255U}) {
        classes.back() = static_cast<std::uint8_t>(classId);
        expectRejectedWithoutAppend();
    }
}

TEST_F(Yolo26SemanticSegmentationParser, RejectsInvalidImageGeometry) {
    const auto validGeometry = input.inferenceInfo.image;
    for (const auto dimensions : {std::array<std::size_t, 2U>{0U, 2U}, {3U, 0U}}) {
        input.inferenceInfo.image.width = dimensions[0];
        input.inferenceInfo.image.height = dimensions[1];
        expectRejectedWithoutAppend();
    }
    input.inferenceInfo.image = validGeometry;
    input.inferenceInfo.image.modelWidth = 1023U;
    expectRejectedWithoutAppend();
    input.inferenceInfo.image = validGeometry;
    input.inferenceInfo.image.modelHeight = 1023U;
    expectRejectedWithoutAppend();
    input.inferenceInfo.image = validGeometry;
    input.inferenceInfo.image.letterboxLeft = ModelSize;
    expectRejectedWithoutAppend();
    input.inferenceInfo.image = validGeometry;
    input.inferenceInfo.image.letterboxLeft = 512U;
    input.inferenceInfo.image.letterboxRight = 512U;
    expectRejectedWithoutAppend();
    input.inferenceInfo.image = validGeometry;
    input.inferenceInfo.image.letterboxTop = std::numeric_limits<std::size_t>::max();
    input.inferenceInfo.image.letterboxBottom = 1U;
    expectRejectedWithoutAppend();
}

TEST_F(Yolo26SemanticSegmentationParser, RejectsMissingOrMismatchedSourceFrame) {
    input.inferenceInfo.parentId = 0U;
    expectRejectedWithoutAppend();
    input.inferenceInfo.parentId = SourceId + 1U;
    expectRejectedWithoutAppend();
    input.inferenceInfo.parentId = SourceId;
    input.inferenceInfo.image.width = 4U;
    expectRejectedWithoutAppend();
    input.inferenceInfo.image.width = 3U;
    output = open_perception_kit::FrameResults{};
    expectRejectedWithoutAppend();
}

TEST_F(Yolo26SemanticSegmentationParser, RejectsRoiAndCroppedFrameParents) {
    open_perception_kit::metadata::BoxDetectionsT boxes;
    auto detection = std::make_unique<open_perception_kit::metadata::BoxDetectionT>();
    detection->object = open_perception_kit::makeObjectMeta(SourceId + 1U, SourceId);
    detection->box = open_perception_kit::makeBoundingBox(0.0F, 0.0F, 2.0F, 2.0F);
    boxes.detections.push_back(std::move(detection));
    output.add(std::move(boxes));
    input.inferenceInfo.parentId = SourceId + 1U;
    expectRejectedWithoutAppend();

    setSourceSize(3U, 2U);
    open_perception_kit::FrameResults cropped;
    open_perception_kit::metadata::FrameContextT frame;
    frame.video = std::make_unique<open_perception_kit::metadata::VideoFrameContextT>();
    frame.video->object = open_perception_kit::makeObjectMeta(SourceId);
    frame.video->original_width = 3U;
    frame.video->original_height = 2U;
    frame.video->source_crop_left = 1U;
    cropped.add(std::move(frame));
    output = std::move(cropped);
    expectRejectedWithoutAppend();
}

TEST_F(Yolo26SemanticSegmentationParser, RejectsBitmapAndPacketSizeOverflowBeforeAllocation) {
    if constexpr (std::numeric_limits<std::size_t>::max() >
                  std::numeric_limits<std::uint32_t>::max()) {
        setSourceSize(static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1U, 1U);
        expectRejectedWithoutAppend();
    }
    setSourceSize(65536U, 65536U);
    expectRejectedWithoutAppend();
}

TEST_F(Yolo26SemanticSegmentationParser, SerializesClassMapAndSourceMetadata) {
    fillClassGrid();
    const auto result = parse();
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    const auto packet = open_perception_kit::serialize(output);
    const open_perception_kit::FrameResults decoded{std::span<const std::uint8_t>(packet)};
    ASSERT_TRUE(decoded.valid());
    EXPECT_EQ(decoded.producer_identity(),
              open_perception_kit::container::producer_identity_status::exact_match);
    EXPECT_EQ(decoded.count<open_perception_kit::metadata::FrameContextT>(), 1U);
    ASSERT_EQ(decoded.count<SemanticSegmentationMasksT>(), 1U);
    const auto ref = decoded.get<SemanticSegmentationMasksT>();
    ASSERT_TRUE(ref.has_value());
    const auto &restored = ref->value();
    ASSERT_NE(restored.layer, nullptr);
    EXPECT_EQ(restored.layer->content_type, "semanticSegmentation");
    EXPECT_EQ(restored.layer->label_family, "cityscapes");
    ASSERT_EQ(restored.masks.size(), 1U);
    const auto &mask = restored.masks[0];
    ASSERT_NE(mask, nullptr);
    ASSERT_NE(mask->object, nullptr);
    EXPECT_EQ(mask->object->parent_id, SourceId);
    ASSERT_NE(mask->bitmap, nullptr);
    EXPECT_EQ(mask->bitmap->width, 3U);
    EXPECT_EQ(mask->bitmap->height, 2U);
    EXPECT_EQ(mask->bitmap->pixels, (std::vector<std::uint8_t>{0U, 1U, 2U, 8U, 9U, 10U}));

    // The Python SDK test requests this packet produced by the actual C++ parser.
    if (const char *path = std::getenv("OPK_YOLO26_SEMANTIC_TEST_PACKET")) {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(stream);
        stream.write(reinterpret_cast<const char *>(packet.data()),
                     static_cast<std::streamsize>(packet.size()));
        ASSERT_TRUE(stream);
    }
}

} // namespace
