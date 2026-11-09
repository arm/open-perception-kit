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
#include "InferenceControllerOp.h"
#include "mediaio/PixelBufferVideoFrame.h"
#include "opk/FrameResults.h"
#include "postproc/ScrfdParser.h"
#include "postproc/UltrafaceParser.h"
#include "tracking_test_helpers.h"
#include <cmath>
#include <cstddef>
#include <gtest/gtest.h>
#include <limits>

namespace {
const opk::ImageInferenceMetadata Crop{.width = 200,
                                       .height = 100,
                                       .frameId = 42,
                                       .frameWidth = 640,
                                       .frameHeight = 480,
                                       .cropX = 100,
                                       .cropY = 50};
}

TEST(ImageGeometry, NormalizedAndPixelCropsProjectIdenticallyAfterResize) {
    const auto normalized =
        open_perception_kit::makeImageBoundingBox(Crop, .25f, .5f, .5f, .25f, true);
    const auto pixels = open_perception_kit::makeImageBoundingBox(Crop, 50, 50, 100, 25);
    const auto first = open_perception_kit::projectBoundingBox(*normalized, 1280, 960);
    const auto second = open_perception_kit::projectBoundingBox(*pixels, 1280, 960);
    const std::array<double, 4> expected{300, 200, 200, 50};
    EXPECT_EQ(first, std::make_optional(expected));
    EXPECT_EQ(second, std::make_optional(expected));
}

TEST(ImageGeometry, RejectsMissingInvalidAndUnknownSpacesWithoutGuessing) {
    auto box = open_perception_kit::makeBoundingBox(1, 2, 3, 4);
    EXPECT_FALSE(open_perception_kit::projectBoundingBox(*box, 640, 480));
    box->space = open_perception_kit::makeImageSpace(Crop);
    box->units = static_cast<open_perception_kit::metadata::CoordinateUnits>(99);
    EXPECT_FALSE(open_perception_kit::projectBoundingBox(*box, 640, 480));
    box->units = open_perception_kit::metadata::CoordinateUnits::Pixels;
    box->space->origin_x = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(open_perception_kit::projectBoundingBox(*box, 640, 480));
    box->space = open_perception_kit::makeImageSpace(Crop);
    box->space->extent_width = 1000;
    EXPECT_FALSE(open_perception_kit::projectBoundingBox(*box, 640, 480));
}

TEST(ImageGeometry, SubjectLookupCountsBoxAndTrackIdentitiesBeforeGeometryValidation) {
    open_perception_kit::metadata::BoxDetectionsT boxes;
    boxes.layer = open_perception_kit::makeLayerInfo({.contentType = "custom"});
    auto box = std::make_unique<open_perception_kit::metadata::BoxDetectionT>();
    box->object = open_perception_kit::makeObjectMeta(7);
    box->box = open_perception_kit::makeImageBoundingBox(Crop, 1, 2, 3, 4);
    boxes.detections.push_back(std::move(box));
    open_perception_kit::FrameResults result;
    result.add(boxes);
    EXPECT_NE(open_perception_kit::findSubjectBox(result, 7), nullptr);
    EXPECT_EQ(open_perception_kit::findSubjectBox(result, 0), nullptr);
    open_perception_kit::metadata::ObjectTracksT tracks;
    auto track = std::make_unique<open_perception_kit::metadata::ObjectTrackT>();
    track->object = open_perception_kit::makeObjectMeta(7);
    track->identity = testTrackIdentity(99);
    track->sample = testTrackingSample(1);
    tracks.tracks.push_back(std::move(track));
    result.add(tracks); // Missing track geometry must not rescue the duplicate ID.
    EXPECT_EQ(open_perception_kit::findSubjectBox(result, 7), nullptr);
    tracks.tracks[0]->box = open_perception_kit::makeImageBoundingBox(Crop, 1, 2, 3, 4);
    open_perception_kit::FrameResults tracked;
    tracked.add(tracks);
    EXPECT_NE(open_perception_kit::findSubjectBox(tracked, 7), nullptr);
    EXPECT_EQ(open_perception_kit::findSubjectBox(tracked, 99), nullptr);
}

TEST(ImageGeometry, TrackerUsesPixelCopiesAndRetainsDetectorCoordinates) {
    open_perception_kit::metadata::BoxDetectionsT payload;
    payload.layer = open_perception_kit::makeLayerInfo({.contentType = "genericObject"});
    auto detection = std::make_unique<open_perception_kit::metadata::BoxDetectionT>();
    detection->object = open_perception_kit::makeObjectMeta(7, 999);
    detection->box = open_perception_kit::makeImageBoundingBox(Crop, .25f, .5f, .5f, .25f, true);
    payload.detections.push_back(std::move(detection));
    open_perception_kit::FrameResults results;
    results.add(std::move(payload));
    std::unique_ptr<open_perception_kit::metadata::ImageSpaceT> basis;
    const auto copies =
        opk::tracker::frameinputs::collectDetections(results, opk::tracker::Config{}, basis);
    ASSERT_EQ(copies.size(), 1U);
    EXPECT_FLOAT_EQ(copies[0].box->x, 150);
    EXPECT_FLOAT_EQ(copies[0].box->y, 100);
    EXPECT_EQ(copies[0].box->units, open_perception_kit::metadata::CoordinateUnits::Pixels);
    EXPECT_DOUBLE_EQ(copies[0].box->space->origin_x, 0);
    EXPECT_DOUBLE_EQ(copies[0].box->space->extent_width, 640);
    results.for_each<open_perception_kit::metadata::BoxDetectionsT>([](const auto &original) {
        EXPECT_EQ(original.detections[0]->box->units,
                  open_perception_kit::metadata::CoordinateUnits::Normalized);
        EXPECT_FLOAT_EQ(original.detections[0]->box->x, .25f);
        EXPECT_DOUBLE_EQ(original.detections[0]->box->space->origin_x, 100);
    });
    open_perception_kit::metadata::BoxDetectionsT resized;
    resized.layer = open_perception_kit::makeLayerInfo({.contentType = "genericObject"});
    auto next = std::make_unique<open_perception_kit::metadata::BoxDetectionT>();
    next->object = open_perception_kit::makeObjectMeta(8, 998);
    next->box = open_perception_kit::makeImageBoundingBox({.width = 400,
                                                           .height = 200,
                                                           .frameId = 43,
                                                           .frameWidth = 1280,
                                                           .frameHeight = 960,
                                                           .cropX = 200,
                                                           .cropY = 100},
                                                          .25f,
                                                          .5f,
                                                          .5f,
                                                          .25f,
                                                          true);
    resized.detections.push_back(std::move(next));
    open_perception_kit::FrameResults nextFrame;
    nextFrame.add(std::move(resized));
    const auto rescaled =
        opk::tracker::frameinputs::collectDetections(nextFrame, opk::tracker::Config{}, basis);
    ASSERT_EQ(rescaled.size(), 1U);
    EXPECT_FLOAT_EQ(rescaled[0].box->x, copies[0].box->x);
    EXPECT_FLOAT_EQ(rescaled[0].box->y, copies[0].box->y);
    EXPECT_FLOAT_EQ(rescaled[0].box->width, copies[0].box->width);
    EXPECT_EQ(rescaled[0].box->space->frame_id, 42U);
}

TEST(ImageGeometry, CropControllerUsesSpatialBasisInsteadOfDerivationParent) {
    open_perception_kit::FrameResults results;
    open_perception_kit::metadata::BoxDetectionsT boxes;
    boxes.layer = open_perception_kit::makeLayerInfo({.contentType = "genericObject"});
    auto detection = std::make_unique<open_perception_kit::metadata::BoxDetectionT>();
    detection->object = open_perception_kit::makeObjectMeta(7, 999);
    detection->box = open_perception_kit::makeImageBoundingBox(Crop, .25f, .5f, .5f, .25f, true);
    boxes.detections.push_back(std::move(detection));
    results.add(std::move(boxes));
    open_perception_kit::FrameResults decoded(open_perception_kit::serialize(results));
    opk::op::OpChainContext context;
    context.frameResults = &decoded;
    context.videoFrames["pipelineVideoFrame"] = opk::mediaio::PixelBufferVideoFrame::take(
        std::vector<uint8_t>(std::size_t{1280} * 960 * 3), 1280, 960);
    opk::stdop::InferenceControllerOp controller;
    opk::AttributeMap attributes;
    attributes.set("contentType", "genericObject");
    ASSERT_TRUE(controller.configure(attributes));
    ASSERT_TRUE(controller.process(context));
    ASSERT_EQ(context.inferenceImageCrops.size(), 1U);
    const auto &crop = context.inferenceImageCrops[0];
    EXPECT_EQ(crop.x, 300U);
    EXPECT_EQ(crop.y, 200U);
    EXPECT_EQ(crop.width, 200U);
    EXPECT_EQ(crop.height, 50U);
    EXPECT_EQ(context.inferenceImageCropIds[0], 7U);
}

TEST(ImageGeometry, FrameIdentityAndPresentationTimeAreSeparateFromCreationTime) {
    open_perception_kit::FrameResults results;
    opk::op::OpChainContext context;
    context.frameResults = &results;
    const auto producer = open_perception_kit::makeProducerInfo("test", "test", "test");
    auto frame = opk::mediaio::PixelBufferVideoFrame::take(std::vector<uint8_t>(12),
                                                           2,
                                                           2,
                                                           opk::RawImagePixelFormat::Rgb,
                                                           0,
                                                           opk::AccessMode::ReadWrite,
                                                           0);
    ASSERT_NE(frame, nullptr);
    context.videoFrames["source"] = std::move(frame);
    const auto id = context.videoFrameId("source", *producer);
    EXPECT_NE(id, 0U);
    EXPECT_EQ(context.videoFrameId("source", *producer), id);
    EXPECT_EQ(results.count<open_perception_kit::metadata::FrameContextT>(), 1U);
    const open_perception_kit::FrameResults decoded(open_perception_kit::serialize(results));
    decoded.for_each<open_perception_kit::metadata::FrameContextT>([id](const auto &payload) {
        ASSERT_NE(payload.video, nullptr);
        EXPECT_EQ(payload.video->presentation_ts_ns.value_or(UINT64_MAX), 0U);
        EXPECT_EQ(payload.video->object->id, id);
        EXPECT_GT(payload.video->object->creation_ts_ns, 0U);
    });
    context.resetVideoFrameIds();
    EXPECT_NE(context.videoFrameId("source", *producer), id);
    auto unknown = opk::mediaio::PixelBufferVideoFrame::take(std::vector<uint8_t>(12), 2, 2);
    ASSERT_NE(unknown, nullptr);
    context.videoFrames["unknown"] = std::move(unknown);
    const auto unknownId = context.videoFrameId("unknown", *producer);
    results.for_each<open_perception_kit::metadata::FrameContextT>(
        [unknownId](const auto &payload) {
            if (payload.video->object->id == unknownId) {
                EXPECT_FALSE(payload.video->presentation_ts_ns.has_value());
            }
        });
}

TEST(ImageGeometry, UltraFaceRemovesLetterboxBeforeProjectingCrop) {
    std::vector<float> scores(std::size_t{4420} * 2);
    std::vector<float> boxes(std::size_t{4420} * 4);
    scores[1] = .99f;
    // Encode one box spanning the central half of the model image at anchor zero.
    boxes[0] = (.5f - .5f / 40) / (.1f * 10 / 320);
    boxes[1] = (.5f - .5f / 30) / (.1f * 10 / 240);
    boxes[2] = std::log(.5f / (10.0f / 320)) / .2f;
    boxes[3] = std::log(.5f / (10.0f / 240)) / .2f;
    opk::TensorView scoreTensor(scores.data(),
                                scores.size() * sizeof(float),
                                opk::Shape(1, 4420, 2),
                                opk::Dtype::Float32,
                                1,
                                0);
    opk::TensorView boxTensor(boxes.data(),
                              boxes.size() * sizeof(float),
                              opk::Shape(1, 4420, 4),
                              opk::Dtype::Float32,
                              1,
                              0);
    opk::AttributeMap attributes;
    opk::TensorParser::Input input(attributes);
    input.inferenceInfo.image = Crop;
    input.inferenceInfo.image.modelWidth = 320;
    input.inferenceInfo.image.modelHeight = 240;
    input.inferenceInfo.image.letterboxTop = 40;
    input.inferenceInfo.image.letterboxBottom = 40;
    input.tensors[0] = &scoreTensor;
    input.tensors[1] = &boxTensor;
    open_perception_kit::FrameResults results;
    opk::stdop::postproc::UltraFaceParser parser;
    ASSERT_TRUE(parser.parse(input, results));
    EXPECT_EQ(results.count<open_perception_kit::metadata::BoxDetectionsT>(), 1U);
    results.for_each<open_perception_kit::metadata::BoxDetectionsT>([](const auto &payload) {
        ASSERT_EQ(payload.detections.size(), 1U);
        const auto pixels =
            open_perception_kit::projectBoundingBox(*payload.detections[0]->box, 640, 480);
        ASSERT_TRUE(pixels.has_value());
        EXPECT_NEAR(pixels.value()[0], 150, .001);
        EXPECT_NEAR(pixels.value()[1], 62.5, .001);
        EXPECT_NEAR(pixels.value()[2], 100, .001);
        EXPECT_NEAR(pixels.value()[3], 75, .001);
    });
}

TEST(ImageGeometry, ScrfdRemovesLetterboxBeforeProjectingCrop) {
    std::vector<float> scores(std::size_t{4} * 4);
    std::vector<float> boxes(std::size_t{4} * 4 * 4);
    scores[10] = .99f;
    std::fill(boxes.begin() + 40, boxes.begin() + 44, 1.0f);
    opk::TensorView scoreTensor(scores.data(),
                                scores.size() * sizeof(float),
                                opk::Shape(1, 4, 4, 1),
                                opk::Dtype::Float32,
                                1,
                                0);
    opk::TensorView boxTensor(boxes.data(),
                              boxes.size() * sizeof(float),
                              opk::Shape(1, 4, 4, 4),
                              opk::Dtype::Float32,
                              1,
                              0);
    opk::AttributeMap attributes;
    opk::TensorParser::Input input(attributes);
    input.inferenceInfo.image = Crop;
    input.inferenceInfo.image.modelWidth = 32;
    input.inferenceInfo.image.modelHeight = 32;
    input.inferenceInfo.image.letterboxTop = 8;
    input.inferenceInfo.image.letterboxBottom = 8;
    input.tensors[0] = &scoreTensor;
    input.tensors[1] = &boxTensor;
    open_perception_kit::FrameResults results;
    opk::stdop::postproc::ScrfdParser parser;
    ASSERT_TRUE(parser.parse(input, results));
    EXPECT_EQ(results.count<open_perception_kit::metadata::BoxDetectionsT>(), 1U);
    results.for_each<open_perception_kit::metadata::BoxDetectionsT>([](const auto &payload) {
        ASSERT_EQ(payload.detections.size(), 1U);
        const auto pixels =
            open_perception_kit::projectBoundingBox(*payload.detections[0]->box, 640, 480);
        ASSERT_TRUE(pixels.has_value());
        const std::array<double, 4> expected{150, 50, 100, 100};
        EXPECT_EQ(pixels.value(), expected);
    });
}
