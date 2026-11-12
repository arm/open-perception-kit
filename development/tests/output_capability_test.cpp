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
#include "op/Op.h"
#include "op/OpChain.h"
#include "op/OpRef.h"
#include "opk/InputBindings.h"
#include <array>
#include <gtest/gtest.h>

namespace {
using namespace open_perception_kit::metadata;
using opk::outputCapability;

TEST(OutputCapabilities, ParsersAdvertiseOnlySdkPayloadTypes) {
    const std::array outputs = {
        std::pair{"CameraContactParser", outputCapability<CameraContactsT>()},
        std::pair{"DummyParser", opk::OutputCapability{}},
        std::pair{"GazeDetectionParser", outputCapability<GazeMeasurementsT>()},
        std::pair{"ImageNetClassificationParser", outputCapability<ClassificationsT>()},
        std::pair{"ModNetSegmentationParser", outputCapability<SegmentationMasksT>()},
        std::pair{"ObjectEmbeddingParser", outputCapability<ObjectEmbeddingsT>()},
        std::pair{"PaddleOcrDetectionParser", outputCapability<SegmentationMasksT>()},
        std::pair{"PaddleOcrDbDetectionParser", outputCapability<BoxDetectionsT>()},
        std::pair{"PaddleOcrRecognitionParser", outputCapability<ClassificationsT>()},
        std::pair{"PersonClassificationParser", outputCapability<PersonPresencesT>()},
        std::pair{"RvmParser", outputCapability<SegmentationMasksT>()},
        std::pair{"RetinaFaceParser", outputCapability<BoxDetectionsT>()},
        std::pair{"ScrfdParser", outputCapability<BoxDetectionsT>()},
        std::pair{"SileroVadParser", outputCapability<VoiceActivityDetectionsT>()},
        std::pair{"SmartTurnParser", outputCapability<TurnDetectionsT>()},
        std::pair{"UltrafaceParser", outputCapability<BoxDetectionsT>()},
        std::pair{"YoloParser", outputCapability<BoxDetectionsT>()},
        std::pair{"YoloXParser", outputCapability<BoxDetectionsT>()},
        std::pair{"Yolo26InstanceSegmentationParser",
                  outputCapability<InstanceSegmentationMasksT>()},
        std::pair{"Yolo26SemanticSegmentationParser",
                  outputCapability<SemanticSegmentationMasksT>()},
    };
    opk::op::OpRef op;
    ASSERT_TRUE(op.bind(OPK_STDOPS_PATH, "GenericPostprocess"));
    const auto *provider = op->as<opk::op::OpInterfacePostprocessor>();
    ASSERT_NE(provider, nullptr);
    for (const auto &[parser, output] : outputs) {
        opk::AttributeMap attrs;
        ASSERT_TRUE(op->configure(attrs.set("parser", parser)));
        EXPECT_EQ(provider->getProvidedOutputs(),
                  output.payloadType.empty() ? std::vector<opk::OutputCapability>{}
                                             : std::vector<opk::OutputCapability>{output});
        if (!output.payloadType.empty())
            EXPECT_TRUE(opk::isKnownPayloadType(output.payloadType));
    }
    EXPECT_FALSE(opk::isKnownPayloadType("humanFace"));
}

TEST(OutputCapabilities, ConnectionsRequireBothExactProducerAndPayload) {
    const auto boxes = outputCapability<BoxDetectionsT>().payloadType;
    EXPECT_TRUE(opk::provides({boxes, "one/output"}, {boxes, "one/output"}));
    EXPECT_FALSE(opk::provides({boxes, "one/output"}, {boxes, "two/output"}));
    EXPECT_FALSE(opk::provides({boxes, "one/output"}, {boxes, ""}));
    EXPECT_FALSE(opk::provides({boxes, "one/output"},
                               {outputCapability<ClassificationsT>().payloadType, "one/output"}));
    EXPECT_FALSE(opk::parseProducerIds("genericObject"));
    EXPECT_FALSE(opk::parseProducerIds("[\"same\",\"same\"]"));
    EXPECT_TRUE(opk::parseProducerIds("[]").has_value());
    EXPECT_FALSE(opk::parseInputBindings(
        R"([{"consumerId":"crop","inputId":"input","payloadType":"humanFace","producerId":"one"}])"));
}

TEST(OutputCapabilities, LocalConnectionsDoNotCollapseSameTypePublishers) {
    opk::op::OpRef first, second, controller;
    ASSERT_TRUE(first.bind(OPK_STDOPS_PATH, "GenericPostprocess"));
    ASSERT_TRUE(second.bind(OPK_STDOPS_PATH, "GenericPostprocess"));
    ASSERT_TRUE(controller.bind(OPK_STDOPS_PATH, "InferenceController"));
    first->instanceId = "first";
    second->instanceId = "second";
    controller->instanceId = "crop";
    opk::AttributeMap parser, source;
    ASSERT_TRUE(first->configure(parser.set("parser", "YoloParser")));
    ASSERT_TRUE(second->configure(parser.set("parser", "RetinaFaceParser")));
    ASSERT_TRUE(controller->configure(source.set("sourceOp", "first")));
    auto *const consumer = controller.get();
    opk::op::OpChain chain;
    chain.add(first);
    chain.add(second);
    chain.add(controller);
    ASSERT_TRUE(chain.bind());
    const auto boxes = outputCapability<BoxDetectionsT>().payloadType;
    EXPECT_EQ(chain.getProvidedOutputs("infer"),
              (std::vector<opk::OutputReference>{{boxes, "infer/first"}, {boxes, "infer/second"}}));
    EXPECT_TRUE(chain.getRequiredInputs("infer").empty());
    EXPECT_FALSE(chain.validateInputBindings({{"crop", "input", boxes, "other/output"}}));
    source.clear();
    ASSERT_TRUE(consumer->configure(source.set("sourceProducer", "")));
    EXPECT_EQ(chain.getRequiredInputs("infer"),
              (std::vector<opk::InputBinding>{{"crop", "input", boxes, ""}}));
    const std::vector<opk::InputBinding> binding{{"crop", "input", boxes, "upstream/output"}};
    EXPECT_TRUE(chain.validateInputBindings(binding));
    EXPECT_EQ(chain.getRequiredInputs("infer", binding), binding);
    EXPECT_FALSE(chain.validateInputBindings({binding.front(), binding.front()}));
    EXPECT_FALSE(chain.validateInputBindings({{"crop", "wrong", boxes, "upstream/output"}}));
    EXPECT_FALSE(chain.validateInputBindings(
        {{"crop", "input", outputCapability<ClassificationsT>().payloadType, "upstream/output"}}));
}

TEST(OutputCapabilities, MissingForwardAndIncompatibleLocalConnectionsFail) {
    for (const std::string parser : {"RetinaFaceParser", "ImageNetClassificationParser"}) {
        for (const bool first : {false, true}) {
            opk::op::OpRef producer, consumer;
            ASSERT_TRUE(producer.bind(OPK_STDOPS_PATH, "GenericPostprocess"));
            ASSERT_TRUE(consumer.bind(OPK_STDOPS_PATH, "InferenceController"));
            producer->instanceId = "source";
            consumer->instanceId = "crop";
            opk::AttributeMap output, input;
            ASSERT_TRUE(producer->configure(output.set("parser", parser)));
            ASSERT_TRUE(consumer->configure(input.set("sourceOp", "source")));
            opk::op::OpChain chain;
            chain.add(first ? producer : consumer);
            chain.add(first ? consumer : producer);
            EXPECT_EQ(bool(chain.bind()), first && parser == "RetinaFaceParser");
        }
    }
    opk::op::OpRef consumer;
    ASSERT_TRUE(consumer.bind(OPK_STDOPS_PATH, "InferenceController"));
    opk::AttributeMap input;
    EXPECT_FALSE(consumer->configure(input.set("contentType", "humanFace")));
    input.clear();
    ASSERT_TRUE(consumer->configure(input.set("sourceOp", "missing")));
    opk::op::OpChain chain;
    chain.add(consumer);
    EXPECT_FALSE(chain.bind());
}
} // namespace
