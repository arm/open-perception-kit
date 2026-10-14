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
#include "postproc/Yolo26InstanceSegmentationParser.h"

namespace {

using open_perception_kit::metadata::InstanceSegmentationMasksT;

constexpr std::size_t DetectionRows = 300U;
constexpr std::size_t DetectionColumns = 38U;
constexpr std::size_t PrototypeChannels = 32U;
constexpr std::size_t PrototypeSize = 160U;
constexpr std::size_t PrototypePlaneSize = PrototypeSize * PrototypeSize;
constexpr std::uint64_t SourceId = 1000000U;

// Captured from the checked-in Python SDK 0.1.1 before the instance metadata extension.
// SGMS identity: 0x1bbd8e0aef4d628f. Raw payload SHA-256:
// d1834c05c7aeda0515c4841b984e72b1f6c7642519bb48f01e3ae3f1cc3ffb1e.
alignas(8) constexpr std::array<std::uint8_t, 232U> HistoricalMattePayload = {
    0x18U, 0x00U, 0x00U, 0x00U, 0x53U, 0x47U, 0x4dU, 0x53U, 0x00U, 0x00U, 0x00U, 0x00U, 0x0cU,
    0x00U, 0x0cU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x08U, 0x00U, 0x04U, 0x00U, 0x0cU, 0x00U,
    0x00U, 0x00U, 0x08U, 0x00U, 0x00U, 0x00U, 0x8cU, 0x00U, 0x00U, 0x00U, 0x01U, 0x00U, 0x00U,
    0x00U, 0x0cU, 0x00U, 0x00U, 0x00U, 0x08U, 0x00U, 0x0cU, 0x00U, 0x08U, 0x00U, 0x04U, 0x00U,
    0x08U, 0x00U, 0x00U, 0x00U, 0x14U, 0x00U, 0x00U, 0x00U, 0x40U, 0x00U, 0x00U, 0x00U, 0x0cU,
    0x00U, 0x14U, 0x00U, 0x10U, 0x00U, 0x0cU, 0x00U, 0x08U, 0x00U, 0x04U, 0x00U, 0x0cU, 0x00U,
    0x00U, 0x00U, 0x10U, 0x00U, 0x00U, 0x00U, 0x14U, 0x00U, 0x00U, 0x00U, 0x02U, 0x00U, 0x00U,
    0x00U, 0x02U, 0x00U, 0x00U, 0x00U, 0x04U, 0x00U, 0x00U, 0x00U, 0x00U, 0x60U, 0xb4U, 0xffU,
    0x05U, 0x00U, 0x00U, 0x00U, 0x55U, 0x69U, 0x6eU, 0x74U, 0x38U, 0x00U, 0x0aU, 0x00U, 0x1eU,
    0x00U, 0x14U, 0x00U, 0x0cU, 0x00U, 0x04U, 0x00U, 0x0aU, 0x00U, 0x00U, 0x00U, 0x0bU, 0x00U,
    0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x03U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
    0x00U, 0x07U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x12U, 0x00U,
    0x0cU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x08U,
    0x00U, 0x04U, 0x00U, 0x12U, 0x00U, 0x00U, 0x00U, 0x08U, 0x00U, 0x00U, 0x00U, 0x20U, 0x00U,
    0x00U, 0x00U, 0x15U, 0x00U, 0x00U, 0x00U, 0x62U, 0x61U, 0x63U, 0x6bU, 0x67U, 0x72U, 0x6fU,
    0x75U, 0x6eU, 0x64U, 0x52U, 0x65U, 0x70U, 0x6cU, 0x61U, 0x63U, 0x65U, 0x6dU, 0x65U, 0x6eU,
    0x74U, 0x00U, 0x00U, 0x00U, 0x0cU, 0x00U, 0x00U, 0x00U, 0x73U, 0x65U, 0x67U, 0x6dU, 0x65U,
    0x6eU, 0x74U, 0x61U, 0x74U, 0x69U, 0x6fU, 0x6eU, 0x00U, 0x00U, 0x00U, 0x00U};

TEST(Yolo26InstanceSegmentationSchema, ReadsHistoricalLegacyMatteBytes) {
    flatbuffers::Verifier verifier(HistoricalMattePayload.data(), HistoricalMattePayload.size());
    ASSERT_TRUE(open_perception_kit::metadata::VerifySegmentationMasksBuffer(verifier));
    const auto *root =
        open_perception_kit::metadata::GetSegmentationMasks(HistoricalMattePayload.data());
    ASSERT_NE(root->masks(), nullptr);
    ASSERT_EQ(root->masks()->size(), 1U);
    const auto *mask = root->masks()->Get(0U);
    ASSERT_NE(mask, nullptr);
    ASSERT_NE(mask->object(), nullptr);
    EXPECT_EQ(mask->object()->id(), 7U);
    EXPECT_EQ(mask->object()->parent_id(), 3U);
    EXPECT_EQ(mask->object()->creation_ts_ns(), 11U);
    ASSERT_NE(mask->bitmap(), nullptr);
    EXPECT_EQ(mask->bitmap()->width(), 2U);
    EXPECT_EQ(mask->bitmap()->height(), 2U);
    ASSERT_NE(mask->bitmap()->pixels(), nullptr);
    const std::vector<std::uint8_t> pixels(mask->bitmap()->pixels()->begin(),
                                           mask->bitmap()->pixels()->end());
    EXPECT_EQ(pixels, (std::vector<std::uint8_t>{0U, 96U, 180U, 255U}));
    ASSERT_NE(root->layer(), nullptr);
    ASSERT_NE(root->layer()->content_type(), nullptr);
    EXPECT_EQ(root->layer()->content_type()->str(), "segmentation");
    ASSERT_NE(root->layer()->compositing_mode(), nullptr);
    EXPECT_EQ(root->layer()->compositing_mode()->str(), "backgroundReplacement");
}

// Golden pixel vectors below are hand-derived from Ultralytics 8.4.38's
// process_mask_native / scale_masks / crop_mask rules, using sparse synthetic prototypes.
// They distinguish resize-before-threshold, align_corners=False interpolation, asymmetric
// border rounding, and the two CPU crop branches without reproducing the parser algorithm.
class Yolo26InstanceSegmentationParser : public ::testing::Test {
  protected:
    Yolo26InstanceSegmentationParser()
        : detections(DetectionRows * DetectionColumns, 0.0f),
          prototypes(PrototypeChannels * PrototypePlaneSize, 0.0f), input(attributes) {}

    void SetUp() override {
        bindTensors();
        input.inferenceInfo.modelName = "yolo26n-seg";
        input.inferenceInfo.inferElementId = "infer-seg";
        input.inferenceInfo.image.modelWidth = 640U;
        input.inferenceInfo.image.modelHeight = 640U;
        input.producerInfo.instance_id = "infer-seg/postprocessor";
        input.producerInfo.component = "opk-std-ops/GenericPostprocess";
        input.producerInfo.implementation = "Yolo26InstanceSegmentationParser";
        setSourceSize(4U, 4U);
    }

    void bindTensors() {
        detectionTensor = opk::TensorView(detections.data(),
                                          detections.size() * sizeof(float),
                                          opk::Shape(1, 300, 38),
                                          opk::Dtype::Float32,
                                          1.0f,
                                          0.0f);
        prototypeTensor = opk::TensorView(prototypes.data(),
                                          prototypes.size() * sizeof(float),
                                          opk::Shape(1, 32, 160, 160),
                                          opk::Dtype::Float32,
                                          1.0f,
                                          0.0f);
        input.tensors[0] = &detectionTensor;
        input.tensors[1] = &prototypeTensor;
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

    void setRow(std::size_t row,
                std::array<float, 4> box = {0.0f, 0.0f, 640.0f, 640.0f},
                float confidence = 0.9f,
                float classId = 7.0f) {
        const auto begin = row * DetectionColumns;
        std::copy(box.begin(), box.end(), detections.begin() + begin);
        detections[begin + 4U] = confidence;
        detections[begin + 5U] = classId;
        detections[begin + 6U] = 1.0f;
    }

    void setPrototype(std::size_t channel, float value) {
        const auto begin = prototypes.begin() + channel * PrototypePlaneSize;
        std::fill(begin, begin + PrototypePlaneSize, value);
    }

    opk::Result<void> parse() {
        return parser.parse(input, output);
    }

    const InstanceSegmentationMasksT *payload() const {
        const auto ref = output.get<InstanceSegmentationMasksT>();
        return ref ? &ref->value() : nullptr;
    }

    void expectPixels(std::size_t index, const std::vector<std::uint8_t> &expected) const {
        const auto *result = payload();
        ASSERT_NE(result, nullptr);
        ASSERT_LT(index, result->masks.size());
        ASSERT_NE(result->masks[index], nullptr);
        ASSERT_NE(result->masks[index]->bitmap, nullptr);
        EXPECT_EQ(result->masks[index]->bitmap->pixels, expected);
    }

    void expectRejectedWithoutAppend() {
        const auto before = output.size();
        const auto result = parse();
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error().flag, opk::ErrorFlag::InvalidData);
        EXPECT_FALSE(result.error().info.empty());
        EXPECT_EQ(output.size(), before);
        EXPECT_EQ(output.count<InstanceSegmentationMasksT>(), 0U);
    }

    opk::AttributeMap attributes;
    std::vector<float> detections;
    std::vector<float> prototypes;
    opk::TensorView detectionTensor;
    opk::TensorView prototypeTensor;
    opk::TensorParser::Input input;
    open_perception_kit::FrameResults output;
    opk::stdop::postproc::Yolo26InstanceSegmentationParser parser;
};

TEST_F(Yolo26InstanceSegmentationParser, CombinesPrototypesAndKeepsOverlappingRowsAssociated) {
    setPrototype(0U, 1.0f);
    for (std::size_t y = 0U; y < PrototypeSize; ++y) {
        for (std::size_t x = 0U; x < PrototypeSize / 2U; ++x) {
            prototypes[PrototypePlaneSize + y * PrototypeSize + x] = -2.0f;
        }
    }
    setRow(0U, {0.0f, 0.0f, 640.0f, 640.0f}, 0.9f, 3.0f);
    detections[7U] = 1.0f;
    setRow(1U, {0.0f, 0.0f, 640.0f, 640.0f}, 0.8f, 79.0f);
    detections[DetectionColumns + 7U] = -1.0f;
    setRow(2U, {0.0f, 0.0f, 640.0f, 640.0f}, 0.25f, 0.0f);

    const auto result = parse();
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    ASSERT_EQ(output.count<InstanceSegmentationMasksT>(), 1U);
    EXPECT_EQ(output.count<open_perception_kit::metadata::SemanticSegmentationMasksT>(), 0U);
    EXPECT_EQ(output.count<open_perception_kit::metadata::SegmentationMasksT>(), 0U);
    ASSERT_EQ(output.count<open_perception_kit::metadata::BoxDetectionsT>(), 0U);
    const auto *segmentation = payload();
    ASSERT_NE(segmentation, nullptr);
    ASSERT_EQ(segmentation->masks.size(), 2U);
    ASSERT_NE(segmentation->layer, nullptr);
    EXPECT_EQ(segmentation->layer->content_type, "instanceSegmentation");
    EXPECT_EQ(segmentation->layer->label_family, "coco");
    EXPECT_EQ(segmentation->layer->model, "yolo26n-seg");
    EXPECT_EQ(segmentation->layer->infer_element_id, "infer-seg");
    ASSERT_NE(segmentation->layer->producer, nullptr);
    EXPECT_EQ(segmentation->layer->producer->instance_id, "infer-seg/postprocessor");
    EXPECT_EQ(segmentation->layer->producer->implementation, "Yolo26InstanceSegmentationParser");
    EXPECT_EQ(segmentation->masks[0]->class_id, 3);
    EXPECT_EQ(segmentation->masks[1]->class_id, 79);
    EXPECT_FLOAT_EQ(segmentation->masks[0]->confidence, 0.9f);
    EXPECT_FLOAT_EQ(segmentation->masks[1]->confidence, 0.8f);
    for (const auto &mask : segmentation->masks) {
        ASSERT_NE(mask->object, nullptr);
        ASSERT_NE(mask->box, nullptr);
        EXPECT_NE(mask->object->id, 0U);
        EXPECT_EQ(mask->object->parent_id, SourceId);
        EXPECT_FLOAT_EQ(mask->box->x, 0.0f);
        EXPECT_FLOAT_EQ(mask->box->y, 0.0f);
        EXPECT_FLOAT_EQ(mask->box->width, 4.0f);
        EXPECT_FLOAT_EQ(mask->box->height, 4.0f);
        EXPECT_EQ(mask->bitmap->width, 4U);
        EXPECT_EQ(mask->bitmap->height, 4U);
        EXPECT_EQ(mask->bitmap->value_type, "Uint8");
    }
    EXPECT_NE(segmentation->masks[0]->object->id, segmentation->masks[1]->object->id);
    expectPixels(0U, {0U, 0U, 1U, 1U, 0U, 0U, 1U, 1U, 0U, 0U, 1U, 1U, 0U, 0U, 1U, 1U});
    expectPixels(1U, std::vector<std::uint8_t>(16U, 1U));
}

TEST_F(Yolo26InstanceSegmentationParser, AppendsAnEmptyPayloadWhenNoRowsExceedThreshold) {
    setPrototype(0U, 1.0f);
    setRow(0U, {0.0f, 0.0f, 640.0f, 640.0f}, 0.25f);
    const auto result = parse();
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    ASSERT_NE(payload(), nullptr);
    EXPECT_TRUE(payload()->masks.empty());
    EXPECT_EQ(output.count<InstanceSegmentationMasksT>(), 1U);
}

TEST_F(Yolo26InstanceSegmentationParser, UsesConfiguredConfidenceThreshold) {
    attributes.set("confidenceThreshold", 0.75);
    setPrototype(0U, 1.0f);
    setRow(0U, {0.0f, 0.0f, 640.0f, 640.0f}, 0.75f);
    setRow(1U, {0.0f, 0.0f, 640.0f, 640.0f}, 0.8f, 2.0f);
    const auto result = parse();
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    ASSERT_NE(payload(), nullptr);
    ASSERT_EQ(payload()->masks.size(), 1U);
    EXPECT_EQ(payload()->masks[0]->class_id, 2);
}

TEST_F(Yolo26InstanceSegmentationParser, FiltersFloat32ScoresEqualToDecimalThreshold) {
    attributes.set("confidenceThreshold", 0.8);
    setPrototype(0U, 1.0f);
    setRow(0U, {0.0f, 0.0f, 640.0f, 640.0f}, 0.8f, 1.0f);
    setRow(1U, {0.0f, 0.0f, 640.0f, 640.0f}, 0.9f, 2.0f);
    const auto result = parse();
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    ASSERT_NE(payload(), nullptr);
    ASSERT_EQ(payload()->masks.size(), 1U);
    EXPECT_EQ(payload()->masks[0]->class_id, 2);
}

TEST_F(Yolo26InstanceSegmentationParser, ThresholdsInterpolatedLogitsRatherThanPrototypePixels) {
    setSourceSize(3U, 3U);
    setPrototype(0U, 1.0f);
    for (std::size_t y = 0U; y < PrototypeSize; ++y) {
        prototypes[y * PrototypeSize + 79U] = -3.0f;
    }
    setRow(0U);
    const auto result = parse();
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    // align_corners=False samples x=79.5 in the middle column: (-3 + 1) / 2 < 0.
    expectPixels(0U, {1U, 0U, 1U, 1U, 0U, 1U, 1U, 0U, 1U});
}

TEST_F(Yolo26InstanceSegmentationParser, RemovesOddAsymmetricLetterboxingBeforeBilinearResize) {
    setSourceSize(3U, 2U);
    input.inferenceInfo.image.letterboxTop = 106U;
    input.inferenceInfo.image.letterboxBottom = 107U;
    setPrototype(0U, 1.0f);
    std::fill(
        prototypes.begin() + 52U * PrototypeSize, prototypes.begin() + 53U * PrototypeSize, -3.0f);
    std::fill(
        prototypes.begin() + 53U * PrototypeSize, prototypes.begin() + 54U * PrototypeSize, 7.0f);
    setRow(0U);
    const auto result = parse();
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    // Ultralytics 8.4.38 crops prototype rows [26,133), then samples y=52.25 and 105.75.
    expectPixels(0U, {0U, 0U, 0U, 1U, 1U, 1U});
    ASSERT_NE(payload(), nullptr);
    ASSERT_EQ(payload()->masks.size(), 1U);
    EXPECT_EQ(payload()->masks[0]->bitmap->width, 3U);
    EXPECT_EQ(payload()->masks[0]->bitmap->height, 2U);
    EXPECT_FLOAT_EQ(payload()->masks[0]->box->height, 2.0f);
}

TEST_F(Yolo26InstanceSegmentationParser, UsesRecordedPaddingFromOpkLetterboxing) {
    setSourceSize(11U, 10U);
    // OPK rounds the active input image to 640x582 and records 29 pixels on both sides.
    input.inferenceInfo.image.letterboxTop = 29U;
    input.inferenceInfo.image.letterboxBottom = 29U;
    setPrototype(0U, 1.0f);
    std::fill(prototypes.begin(), prototypes.begin() + 13U * PrototypeSize, -1.0f);
    std::fill(
        prototypes.begin() + 13U * PrototypeSize, prototypes.begin() + 14U * PrototypeSize, -3.0f);
    setRow(0U);
    const auto result = parse();
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    // Prototype crop [7,153) yields first sample y=13.8, whose logit is positive.
    // Cropping to [7,152) yields y=13.75 and a zero logit; keeping padding samples negative rows.
    expectPixels(0U, std::vector<std::uint8_t>(110U, 1U));
    ASSERT_NE(payload(), nullptr);
    ASSERT_EQ(payload()->masks.size(), 1U);
    EXPECT_EQ(payload()->masks[0]->bitmap->width, 11U);
    EXPECT_EQ(payload()->masks[0]->bitmap->height, 10U);
    EXPECT_FLOAT_EQ(payload()->masks[0]->box->height, 10.0f);
}

TEST_F(Yolo26InstanceSegmentationParser, RoundsFractionalCpuCropEdgesToEvenForSmallBatches) {
    setPrototype(0U, 1.0f);
    setRow(0U, {80.0f, 240.0f, 400.0f, 560.0f});
    const auto result = parse();
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    // Source edges (.5,1.5,2.5,3.5) round to (0,2,2,4) in the <50 CPU branch.
    expectPixels(0U, {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U, 1U, 0U, 0U, 1U, 1U, 0U, 0U});
    ASSERT_NE(payload(), nullptr);
    ASSERT_EQ(payload()->masks.size(), 1U);
    EXPECT_FLOAT_EQ(payload()->masks[0]->box->x, 0.5f);
    EXPECT_FLOAT_EQ(payload()->masks[0]->box->y, 1.5f);
    EXPECT_FLOAT_EQ(payload()->masks[0]->box->width, 2.0f);
    EXPECT_FLOAT_EQ(payload()->masks[0]->box->height, 2.0f);
}

TEST_F(Yolo26InstanceSegmentationParser, UsesHalfOpenFloatingCropForFiftyRetainedRows) {
    setPrototype(0U, 1.0f);
    for (std::size_t row = 0U; row < 50U; ++row) {
        setRow(row, {80.0f, 240.0f, 400.0f, 560.0f}, 0.9f, static_cast<float>(row));
    }
    const auto result = parse();
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    ASSERT_NE(payload(), nullptr);
    ASSERT_EQ(payload()->masks.size(), 50U);
    const std::vector<std::uint8_t> expected = {
        0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U, 1U, 0U, 0U, 1U, 1U, 0U};
    expectPixels(0U, expected);
    expectPixels(49U, expected);
    EXPECT_EQ(payload()->masks[49U]->class_id, 49);
}

TEST_F(Yolo26InstanceSegmentationParser, SelectsCropBranchBeforeDroppingEmptyMasks) {
    setPrototype(0U, 1.0f);
    for (std::size_t row = 0U; row < 50U; ++row) {
        setRow(row, {80.0f, 240.0f, 400.0f, 560.0f});
    }
    detections[49U * DetectionColumns + 6U] = 0.0f;
    const auto result = parse();
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    ASSERT_NE(payload(), nullptr);
    ASSERT_EQ(payload()->masks.size(), 49U);
    expectPixels(0U, {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U, 1U, 0U, 0U, 1U, 1U, 0U});
}

TEST_F(Yolo26InstanceSegmentationParser, SelectsCropBranchBeforeDroppingDegenerateBoxes) {
    setPrototype(0U, 1.0f);
    for (std::size_t row = 0U; row < 49U; ++row) {
        setRow(row, {80.0f, 240.0f, 400.0f, 560.0f});
    }
    setRow(49U, {0.0f, 0.0f, 0.0f, 640.0f});
    const auto result = parse();
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    ASSERT_NE(payload(), nullptr);
    ASSERT_EQ(payload()->masks.size(), 49U);
    expectPixels(0U, {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U, 1U, 0U, 0U, 1U, 1U, 0U});
}

TEST_F(Yolo26InstanceSegmentationParser, ClipsBoxesToSourceEdgesAndDropsDegenerateRows) {
    setPrototype(0U, 1.0f);
    setRow(0U, {320.0f, -20.0f, 700.0f, 660.0f});
    setRow(1U, {-100.0f, -100.0f, -10.0f, -10.0f});
    setRow(2U, {320.0f, 0.0f, 160.0f, 640.0f});
    setRow(3U, {0.0f, 0.0f, 0.0f, 640.0f});
    const auto result = parse();
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    ASSERT_NE(payload(), nullptr);
    ASSERT_EQ(payload()->masks.size(), 1U);
    EXPECT_FLOAT_EQ(payload()->masks[0]->box->x, 2.0f);
    EXPECT_FLOAT_EQ(payload()->masks[0]->box->y, 0.0f);
    EXPECT_FLOAT_EQ(payload()->masks[0]->box->width, 2.0f);
    EXPECT_FLOAT_EQ(payload()->masks[0]->box->height, 4.0f);
    expectPixels(0U, {0U, 0U, 1U, 1U, 0U, 0U, 1U, 1U, 0U, 0U, 1U, 1U, 0U, 0U, 1U, 1U});
}

TEST_F(Yolo26InstanceSegmentationParser, DropsMasksWhoseLogitsAreZeroOrNegative) {
    setPrototype(0U, 1.0f);
    setRow(0U);
    detections[6U] = 0.0f;
    setRow(1U);
    detections[DetectionColumns + 6U] = -1.0f;
    const auto result = parse();
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    ASSERT_NE(payload(), nullptr);
    EXPECT_TRUE(payload()->masks.empty());
}

TEST_F(Yolo26InstanceSegmentationParser, RejectsMissingOrTruncatedTensorBacking) {
    for (const auto index : {0U, 1U}) {
        SCOPED_TRACE(index);
        bindTensors();
        input.tensors[index] = nullptr;
        expectRejectedWithoutAppend();
        bindTensors();
        const auto &values = index == 0U ? detections : prototypes;
        const auto shape = index == 0U ? opk::Shape(1, 300, 38) : opk::Shape(1, 32, 160, 160);
        opk::TensorView truncated(values.data(),
                                  values.size() * sizeof(float) - sizeof(float),
                                  shape,
                                  opk::Dtype::Float32,
                                  1.0f,
                                  0.0f);
        input.tensors[index] = &truncated;
        expectRejectedWithoutAppend();
    }
}

TEST_F(Yolo26InstanceSegmentationParser, RejectsShapesOutsideThePinnedExportContract) {
    const std::array invalidDetectionShapes = {opk::Shape(1, 300, 37),
                                               opk::Shape(1, 38, 300),
                                               opk::Shape(2, 300, 38),
                                               opk::Shape(300, 38)};
    for (const auto &shape : invalidDetectionShapes) {
        bindTensors();
        opk::TensorView invalid(detections.data(),
                                detections.size() * sizeof(float),
                                shape,
                                opk::Dtype::Float32,
                                1.0f,
                                0.0f);
        input.tensors[0] = &invalid;
        expectRejectedWithoutAppend();
    }
    const std::array invalidPrototypeShapes = {
        opk::Shape(1, 31, 160, 160), opk::Shape(1, 32, 159, 160), opk::Shape(32, 160, 160)};
    for (const auto &shape : invalidPrototypeShapes) {
        bindTensors();
        opk::TensorView invalid(prototypes.data(),
                                prototypes.size() * sizeof(float),
                                shape,
                                opk::Dtype::Float32,
                                1.0f,
                                0.0f);
        input.tensors[1] = &invalid;
        expectRejectedWithoutAppend();
    }
}

TEST_F(Yolo26InstanceSegmentationParser, RejectsNonFloat32OutputTypes) {
    for (const auto index : {0U, 1U}) {
        for (const auto type : {opk::Dtype::Uint8, opk::Dtype::Float16}) {
            bindTensors();
            const auto &values = index == 0U ? detections : prototypes;
            const auto shape = index == 0U ? opk::Shape(1, 300, 38) : opk::Shape(1, 32, 160, 160);
            opk::TensorView invalid(
                values.data(), values.size() * sizeof(float), shape, type, 1.0f, 0.0f);
            input.tensors[index] = &invalid;
            expectRejectedWithoutAppend();
        }
    }
}

TEST_F(Yolo26InstanceSegmentationParser, RejectsInvalidScoresEvenOnFilteredRows) {
    for (const auto score : {-0.1f,
                             1.1f,
                             std::numeric_limits<float>::quiet_NaN(),
                             std::numeric_limits<float>::infinity()}) {
        detections[4U] = score;
        expectRejectedWithoutAppend();
    }
}

TEST_F(Yolo26InstanceSegmentationParser, RejectsMalformedRetainedRowsAtomically) {
    setPrototype(0U, 1.0f);
    setRow(0U);
    for (const auto field : {0U, 1U, 2U, 3U, 5U, 6U, 37U}) {
        SCOPED_TRACE(field);
        setRow(1U);
        detections[DetectionColumns + field] = std::numeric_limits<float>::quiet_NaN();
        expectRejectedWithoutAppend();
        detections[DetectionColumns + field] = 0.0f;
    }
    for (const auto classId : {-1.0f, 80.0f, 1.5f}) {
        setRow(1U, {0.0f, 0.0f, 640.0f, 640.0f}, 0.8f, classId);
        expectRejectedWithoutAppend();
    }
}

TEST_F(Yolo26InstanceSegmentationParser, RejectsNonFinitePrototypesEvenForEmptyDetections) {
    for (const auto value :
         {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        prototypes.back() = value;
        expectRejectedWithoutAppend();
    }
}

TEST_F(Yolo26InstanceSegmentationParser, RejectsOverflowInReconstructedLogitsWithoutAppending) {
    setPrototype(0U, 2.0f);
    setRow(0U);
    detections[6U] = std::numeric_limits<float>::max();
    expectRejectedWithoutAppend();
}

TEST_F(Yolo26InstanceSegmentationParser, RejectsInvalidConfidenceThreshold) {
    for (const auto threshold : {-0.1, 1.1, std::numeric_limits<double>::quiet_NaN()}) {
        attributes.set("confidenceThreshold", threshold);
        expectRejectedWithoutAppend();
    }
}

TEST_F(Yolo26InstanceSegmentationParser, RejectsInvalidImageGeometry) {
    const auto validGeometry = input.inferenceInfo.image;
    input.inferenceInfo.image.width = 0U;
    expectRejectedWithoutAppend();
    input.inferenceInfo.image = validGeometry;
    input.inferenceInfo.image.modelWidth = 639U;
    expectRejectedWithoutAppend();
    input.inferenceInfo.image = validGeometry;
    input.inferenceInfo.image.letterboxLeft = 640U;
    expectRejectedWithoutAppend();
    input.inferenceInfo.image = validGeometry;
    input.inferenceInfo.image.letterboxTop = std::numeric_limits<std::size_t>::max();
    input.inferenceInfo.image.letterboxBottom = 1U;
    expectRejectedWithoutAppend();
}

TEST_F(Yolo26InstanceSegmentationParser, RejectsMissingOrMismatchedSourceFrame) {
    input.inferenceInfo.parentId = SourceId + 1U;
    expectRejectedWithoutAppend();
    input.inferenceInfo.parentId = 0U;
    expectRejectedWithoutAppend();
    input.inferenceInfo.parentId = SourceId;
    input.inferenceInfo.image.width = 5U;
    expectRejectedWithoutAppend();
    input.inferenceInfo.image.width = 4U;
    output = open_perception_kit::FrameResults{};
    expectRejectedWithoutAppend();
}

TEST_F(Yolo26InstanceSegmentationParser, RejectsRoiParentsRatherThanGuessingSourceCoordinates) {
    open_perception_kit::metadata::BoxDetectionsT boxes;
    auto detection = std::make_unique<open_perception_kit::metadata::BoxDetectionT>();
    detection->object = open_perception_kit::makeObjectMeta(SourceId + 1U, SourceId);
    detection->box = open_perception_kit::makeBoundingBox(1.0f, 1.0f, 2.0f, 2.0f);
    boxes.detections.push_back(std::move(detection));
    output.add(std::move(boxes));
    input.inferenceInfo.parentId = SourceId + 1U;
    input.inferenceInfo.image.width = 2U;
    input.inferenceInfo.image.height = 2U;
    expectRejectedWithoutAppend();
}

TEST_F(Yolo26InstanceSegmentationParser, RejectsDimensionsUnrepresentableByBitmapData) {
    if constexpr (std::numeric_limits<std::size_t>::max() >
                  std::numeric_limits<std::uint32_t>::max()) {
        setSourceSize(static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1U, 1U);
        expectRejectedWithoutAppend();
    }
}

TEST_F(Yolo26InstanceSegmentationParser, RejectsPacketSizeOverflowBeforeAllocatingMasks) {
    setSourceSize(65536U, 65536U);
    setPrototype(0U, 1.0f);
    setRow(0U);
    expectRejectedWithoutAppend();
}

TEST_F(Yolo26InstanceSegmentationParser, SerializesDistinctSegmentationPayloads) {
    setPrototype(0U, 1.0f);
    setRow(0U, {160.0f, 160.0f, 480.0f, 480.0f});
    const auto result = parse();
    ASSERT_TRUE(result.has_value()) << result.error().toString();
    ASSERT_NE(payload(), nullptr);
    ASSERT_EQ(payload()->masks.size(), 1U);
    const auto instanceId = payload()->masks[0]->object->id;

    open_perception_kit::metadata::SegmentationMasksT matte;
    matte.layer = open_perception_kit::makeLayerInfo(
        {.contentType = "segmentation", .compositingMode = "backgroundReplacement"});
    auto mask = std::make_unique<open_perception_kit::metadata::SegmentationMaskT>();
    mask->object = open_perception_kit::makeObjectMeta(0U, SourceId);
    mask->bitmap = std::make_unique<open_perception_kit::metadata::BitmapDataT>();
    mask->bitmap->width = 2U;
    mask->bitmap->height = 2U;
    mask->bitmap->value_type = "Uint8";
    mask->bitmap->pixels = {0U, 96U, 180U, 255U};
    matte.masks.push_back(std::move(mask));
    output.add(std::move(matte));

    open_perception_kit::metadata::SemanticSegmentationMasksT semantic;
    semantic.layer = open_perception_kit::makeLayerInfo(
        {.contentType = "semanticSegmentation", .labelFamily = "cityscapes"});
    auto classMap = std::make_unique<open_perception_kit::metadata::SemanticSegmentationMaskT>();
    classMap->object = open_perception_kit::makeObjectMeta(0U, SourceId);
    classMap->bitmap = std::make_unique<open_perception_kit::metadata::BitmapDataT>();
    classMap->bitmap->width = 4U;
    classMap->bitmap->height = 4U;
    classMap->bitmap->value_type = "Uint8";
    classMap->bitmap->pixels = {
        0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 18U};
    semantic.masks.push_back(std::move(classMap));
    output.add(std::move(semantic));

    const auto packet = open_perception_kit::serialize(output);
    const open_perception_kit::FrameResults decoded{std::span<const std::uint8_t>(packet)};
    ASSERT_TRUE(decoded.valid());
    EXPECT_EQ(decoded.producer_identity(),
              open_perception_kit::container::producer_identity_status::exact_match);
    EXPECT_EQ(decoded.size(), 4U);
    ASSERT_EQ(decoded.count<InstanceSegmentationMasksT>(), 1U);
    ASSERT_EQ(decoded.count<open_perception_kit::metadata::SegmentationMasksT>(), 1U);
    ASSERT_EQ(decoded.count<open_perception_kit::metadata::SemanticSegmentationMasksT>(), 1U);
    const auto instanceRef = decoded.get<InstanceSegmentationMasksT>();
    ASSERT_TRUE(instanceRef.has_value());
    const auto &instance = instanceRef->value();
    ASSERT_EQ(instance.masks.size(), 1U);
    const auto &restored = *instance.masks[0];
    EXPECT_EQ(restored.object->id, instanceId);
    EXPECT_EQ(restored.object->parent_id, SourceId);
    EXPECT_EQ(restored.class_id, 7);
    EXPECT_FLOAT_EQ(restored.confidence, 0.9f);
    EXPECT_FLOAT_EQ(restored.box->x, 1.0f);
    EXPECT_FLOAT_EQ(restored.box->y, 1.0f);
    EXPECT_FLOAT_EQ(restored.box->width, 2.0f);
    EXPECT_FLOAT_EQ(restored.box->height, 2.0f);
    EXPECT_EQ(restored.bitmap->pixels,
              (std::vector<std::uint8_t>{
                  0U, 0U, 0U, 0U, 0U, 1U, 1U, 0U, 0U, 1U, 1U, 0U, 0U, 0U, 0U, 0U}));

    const auto matteRef = decoded.get<open_perception_kit::metadata::SegmentationMasksT>();
    ASSERT_TRUE(matteRef.has_value());
    ASSERT_EQ(matteRef->value().masks.size(), 1U);
    const auto &restoredMatte = *matteRef->value().masks[0];
    EXPECT_EQ(restoredMatte.bitmap->pixels, (std::vector<std::uint8_t>{0U, 96U, 180U, 255U}));
    EXPECT_EQ(matteRef->value().layer->compositing_mode, "backgroundReplacement");

    const auto semanticRef =
        decoded.get<open_perception_kit::metadata::SemanticSegmentationMasksT>();
    ASSERT_TRUE(semanticRef.has_value());
    EXPECT_EQ(semanticRef->value().layer->content_type, "semanticSegmentation");
    EXPECT_EQ(semanticRef->value().layer->label_family, "cityscapes");
    ASSERT_EQ(semanticRef->value().masks.size(), 1U);
    const auto &restoredSemantic = *semanticRef->value().masks[0];
    EXPECT_EQ(restoredSemantic.object->parent_id, SourceId);
    EXPECT_EQ(restoredSemantic.bitmap->pixels,
              (std::vector<std::uint8_t>{
                  0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 18U}));

    // The Python SDK test requests this real C++ parser packet through a temporary path.
    if (const char *path = std::getenv("OPK_YOLO26_INSTANCE_TEST_PACKET")) {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(stream);
        stream.write(reinterpret_cast<const char *>(packet.data()),
                     static_cast<std::streamsize>(packet.size()));
        ASSERT_TRUE(stream);
    }
}

} // namespace
