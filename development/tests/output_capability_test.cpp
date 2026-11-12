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
#include <string_view>
#include <utility>
#include <vector>

#include "op/Op.h"
#include "op/OpChain.h"
#include "op/OpRef.h"

namespace {
using namespace open_perception_kit::metadata;
using opk::outputCapability;

TEST(OutputCapabilities, ParsersAdvertiseTypedFrameResults) {
    const std::array parserOutputs = {
        std::pair{"CameraContactParser", outputCapability<CameraContactsT>("cameraContact")},
        std::pair{"DummyParser", opk::OutputCapability{}},
        std::pair{"GazeDetectionParser", outputCapability<GazeMeasurementsT>("eyeYawPitch")},
        std::pair{"ImageNetClassificationParser",
                  outputCapability<ClassificationsT>("classification")},
        std::pair{"ModNetSegmentationParser", outputCapability<SegmentationMasksT>("segmentation")},
        std::pair{"ObjectEmbeddingParser", outputCapability<ObjectEmbeddingsT>("objectEmbedding")},
        std::pair{"PaddleOcrDetectionParser", outputCapability<SegmentationMasksT>("segmentation")},
        std::pair{"PaddleOcrDbDetectionParser", outputCapability<BoxDetectionsT>("textRegion")},
        std::pair{"PaddleOcrRecognitionParser",
                  outputCapability<ClassificationsT>("textRecognition")},
        std::pair{"PersonClassificationParser",
                  outputCapability<PersonPresencesT>("personClassification")},
        std::pair{"RvmParser", outputCapability<SegmentationMasksT>("segmentation")},
        std::pair{"RetinaFaceParser", outputCapability<BoxDetectionsT>("humanFace")},
        std::pair{"ScrfdParser", outputCapability<BoxDetectionsT>("humanFace")},
        std::pair{"SileroVadParser", outputCapability<VoiceActivityDetectionsT>("voiceActivity")},
        std::pair{"SmartTurnParser", outputCapability<TurnDetectionsT>("turnDetection")},
        std::pair{"UltrafaceParser", outputCapability<BoxDetectionsT>("humanFace")},
        std::pair{"YoloParser", outputCapability<BoxDetectionsT>("genericObject")},
        std::pair{"YoloXParser", outputCapability<BoxDetectionsT>("genericObject")},
        std::pair{"Yolo26InstanceSegmentationParser",
                  outputCapability<InstanceSegmentationMasksT>("instanceSegmentation")},
        std::pair{"Yolo26SemanticSegmentationParser",
                  outputCapability<SemanticSegmentationMasksT>("semanticSegmentation")},
    };

    opk::op::OpRef op;
    ASSERT_TRUE(op.bind(OPK_STDOPS_PATH, "GenericPostprocess"));
    auto *postprocessor = op->as<opk::op::OpInterfacePostprocessor>();
    ASSERT_NE(postprocessor, nullptr);
    EXPECT_TRUE(postprocessor->getProvidedOutputs().empty());

    for (const auto &[parser, output] : parserOutputs) {
        opk::AttributeMap attributes;
        attributes.set("parser", parser);
        ASSERT_TRUE(op->configure(attributes)) << parser;

        const auto expected = output.payloadType.empty()
                                  ? std::vector<opk::OutputCapability>{}
                                  : std::vector<opk::OutputCapability>{output};
        EXPECT_EQ(postprocessor->getProvidedOutputs(), expected) << parser;
    }
}

TEST(OutputCapabilities, InferenceControllerRequiresBoxesNotJustAMatchingRole) {
    opk::op::OpRef op;
    ASSERT_TRUE(op.bind(OPK_STDOPS_PATH, "InferenceController"));
    auto *consumer = op->as<opk::op::OpInterfaceResultConsumer>();
    ASSERT_NE(consumer, nullptr);

    opk::AttributeMap attributes;
    ASSERT_TRUE(op->configure(attributes));
    EXPECT_TRUE(consumer->getRequiredOutputs().empty());

    attributes.set("contentType", "humanFace");
    ASSERT_TRUE(op->configure(attributes));
    const auto required = outputCapability<BoxDetectionsT>("humanFace");
    EXPECT_EQ(consumer->getRequiredOutputs(), std::vector<opk::OutputCapability>{required});
    EXPECT_FALSE(opk::provides(outputCapability<ClassificationsT>("humanFace"), required));
    EXPECT_TRUE(opk::provides(required, outputCapability<BoxDetectionsT>()));
    EXPECT_FALSE(opk::provides(outputCapability<BoxDetectionsT>(), required));
}

TEST(OutputCapabilities, ChainReportsOnlyExternalDependenciesAndDeduplicatesOutputs) {
    opk::op::OpRef yolo;
    opk::op::OpRef yoloX;
    opk::op::OpRef dummy;
    opk::op::OpRef controller;
    ASSERT_TRUE(yolo.bind(OPK_STDOPS_PATH, "GenericPostprocess"));
    ASSERT_TRUE(yoloX.bind(OPK_STDOPS_PATH, "GenericPostprocess"));
    ASSERT_TRUE(dummy.bind(OPK_STDOPS_PATH, "GenericPostprocess"));
    ASSERT_TRUE(controller.bind(OPK_STDOPS_PATH, "InferenceController"));

    opk::AttributeMap attributes;
    ASSERT_TRUE(yolo->configure(attributes.set("parser", "YoloParser")));
    ASSERT_TRUE(yoloX->configure(attributes.set("parser", "YoloXParser")));
    ASSERT_TRUE(dummy->configure(attributes.set("parser", "DummyParser")));
    attributes.clear();
    ASSERT_TRUE(controller->configure(attributes.set("contentType", "humanFace")));

    opk::op::OpChain chain;
    auto *controllerPtr = controller.get();
    chain.add(yolo);
    chain.add(yoloX);
    chain.add(dummy);
    chain.add(controller);
    ASSERT_TRUE(chain.bind());

    EXPECT_EQ(
        chain.getProvidedOutputs(),
        std::vector<opk::OutputCapability>{outputCapability<BoxDetectionsT>("genericObject")});
    EXPECT_EQ(chain.getRequiredOutputs(),
              std::vector<opk::OutputCapability>{outputCapability<BoxDetectionsT>("humanFace")});
    ASSERT_TRUE(controllerPtr->configure(attributes.set("contentType", "genericObject")));
    EXPECT_TRUE(chain.getRequiredOutputs().empty());
}

TEST(OutputCapabilities, AProviderAfterTheConsumerCannotSatisfyItsInput) {
    opk::op::OpRef controller, producer;
    ASSERT_TRUE(controller.bind(OPK_STDOPS_PATH, "InferenceController"));
    ASSERT_TRUE(producer.bind(OPK_STDOPS_PATH, "GenericPostprocess"));
    opk::AttributeMap input, output;
    ASSERT_TRUE(controller->configure(input.set("contentType", "genericObject")));
    ASSERT_TRUE(producer->configure(output.set("parser", "YoloParser")));
    opk::op::OpChain chain;
    chain.add(controller);
    chain.add(producer);
    ASSERT_TRUE(chain.bind());
    EXPECT_EQ(
        chain.getRequiredOutputs(),
        std::vector<opk::OutputCapability>{outputCapability<BoxDetectionsT>("genericObject")});
}

TEST(OutputCapabilities, RetinaFaceSatisfiesOnlyFollowingFaceConsumers) {
    for (const bool provideFirst : std::array{false, true}) {
        opk::op::OpRef detector, controller;
        ASSERT_TRUE(detector.bind(OPK_STDOPS_PATH, "GenericPostprocess"));
        ASSERT_TRUE(controller.bind(OPK_STDOPS_PATH, "InferenceController"));
        opk::AttributeMap output, input;
        ASSERT_TRUE(detector->configure(output.set("parser", "RetinaFaceParser")));
        ASSERT_TRUE(controller->configure(input.set("contentType", "humanFace")));
        opk::op::OpChain chain;
        chain.add(provideFirst ? detector : controller);
        chain.add(provideFirst ? controller : detector);
        ASSERT_TRUE(chain.bind());
        const std::vector<opk::OutputCapability> faces{
            outputCapability<BoxDetectionsT>("humanFace")};
        EXPECT_EQ(chain.getProvidedOutputs(), faces);
        EXPECT_EQ(chain.getRequiredOutputs(),
                  provideFirst ? std::vector<opk::OutputCapability>{} : faces);
    }
}

} // namespace
