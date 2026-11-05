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

#include "op/OpChainDescriptor.h"
#include "opk/TensorParser.h"
#include "postproc/UltrafaceParser.h"

#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

TEST(UltraFaceParser, LogitsMatchProbabilitiesAndRemainStable) {
    for (const bool logits : {false, true}) {
        opk::AttributeMap attributes;
        attributes.set("scoresAreLogits", logits);
        attributes.set("confidenceThreshold", 0.9);

        std::vector<float> scores(4420 * 2, logits ? -1000.0f : 0.0f);
        for (size_t i = 0; i < 4420; ++i)
            scores[2 * i] = logits ? 1000.0f : 1.0f;

        scores[0] = logits ? 1000.0f : 0.01f;
        scores[1] = logits ? 1000.0f + std::log(99.0f) : 0.99f;

        const std::vector<float> boxes(4420 * 4, 0.0f);
        opk::TensorView scoreTensor(scores.data(),
                                    scores.size() * sizeof(float),
                                    opk::Shape(1, 4420, 2),
                                    opk::Dtype::Float32,
                                    1.0f,
                                    0.0f);
        opk::TensorView boxTensor(boxes.data(),
                                  boxes.size() * sizeof(float),
                                  opk::Shape(1, 4420, 4),
                                  opk::Dtype::Float32,
                                  1.0f,
                                  0.0f);

        opk::TensorParser::Input input(attributes);
        input.tensors[0] = &scoreTensor;
        input.tensors[1] = &boxTensor;
        input.inferenceInfo.image.width = input.inferenceInfo.image.modelWidth = 320;
        input.inferenceInfo.image.height = input.inferenceInfo.image.modelHeight = 240;

        open_perception_kit::FrameResults results;
        opk::stdop::postproc::UltraFaceParser parser;
        ASSERT_TRUE(parser.parse(input, results));

        const auto payload = results.get<open_perception_kit::metadata::BoxDetectionsT>();
        ASSERT_TRUE(payload);
        const auto &detections = payload.value().value().detections;
        ASSERT_EQ(detections.size(), 1U);
        EXPECT_NEAR(detections[0]->confidence, 0.99f, 0.00001f);
    }
}

TEST(UltraFaceParser, PublishedOnnxConfigurationConvertsCapturedLogits) {
    const auto chain =
        opk::op::OpChainDescriptor::fromFile("config/models/ultraface-rfb-320/opchain.json");
    ASSERT_TRUE(chain) << chain.error().info;
    const auto postprocess = std::ranges::find_if(
        chain->ops, [](const auto &op) { return op.id == "opk-std-ops/GenericPostprocess"; });
    ASSERT_NE(postprocess, chain->ops.end());

    std::vector<float> scores(4420 * 2, -1000.0f);
    for (size_t i = 0; i < 4420; ++i)
        scores[2 * i] = 1000.0f;

    // Foreground/background score pairs from the pinned ONNX model (SHA-256
    // 0d8e17c3ff0de020607a8460241f45ff6b08a8ff38c86c48becb665d9b265e01),
    // GettyImages-1140581459.mov at 0 s, anchors 3756/122. Boxes are synthetic.
    scores[0] = -3.236875057220459f;
    scores[1] = 3.1505584716796875f;
    scores[2 * 3000] = 0.7768499851226807f;
    scores[2 * 3000 + 1] = -0.7768499851226807f;
    const std::vector<float> boxes(4420 * 4, 0.0f);
    opk::TensorView scoreTensor(scores.data(),
                                scores.size() * sizeof(float),
                                opk::Shape(1, 4420, 2),
                                opk::Dtype::Float32,
                                1.0f,
                                0.0f);
    opk::TensorView boxTensor(boxes.data(),
                              boxes.size() * sizeof(float),
                              opk::Shape(1, 4420, 4),
                              opk::Dtype::Float32,
                              1.0f,
                              0.0f);

    opk::TensorParser::Input input(postprocess->attributes);
    input.tensors[0] = &scoreTensor;
    input.tensors[1] = &boxTensor;
    input.inferenceInfo.image.width = input.inferenceInfo.image.modelWidth = 320;
    input.inferenceInfo.image.height = input.inferenceInfo.image.modelHeight = 240;

    open_perception_kit::FrameResults results;
    opk::stdop::postproc::UltraFaceParser parser;
    ASSERT_TRUE(parser.parse(input, results));

    const auto payload = results.get<open_perception_kit::metadata::BoxDetectionsT>();
    ASSERT_TRUE(payload);
    const auto &detections = payload.value().value().detections;
    ASSERT_EQ(detections.size(), 1U);
    EXPECT_NEAR(detections[0]->confidence, 0.9983203f, 0.000001f);
    EXPECT_GE(detections[0]->confidence, 0.0f);
    EXPECT_LE(detections[0]->confidence, 1.0f);
}
