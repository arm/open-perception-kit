/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

TEST(StandardContentTypes, ParsersAdvertiseTheirFrameResultsContent) {
    constexpr std::array parserContentTypes = {
        std::pair{"CameraContactParser", "cameraContact"},
        std::pair{"DummyParser", ""},
        std::pair{"GazeDetectionParser", "eyeYawPitch"},
        std::pair{"ImageNetClassificationParser", "classification"},
        std::pair{"ModNetSegmentationParser", "segmentation"},
        std::pair{"ObjectEmbeddingParser", "objectEmbedding"},
        std::pair{"PaddleOcrDetectionParser", "segmentation"},
        std::pair{"PersonClassificationParser", "personClassification"},
        std::pair{"RvmParser", "segmentation"},
        std::pair{"ScrfdParser", "humanFace"},
        std::pair{"UltrafaceParser", "humanFace"},
        std::pair{"YoloParser", "genericObject"},
        std::pair{"YoloXParser", "genericObject"},
    };

    opk::op::OpRef op;
    ASSERT_TRUE(op.bind(OPK_STDOPS_PATH, "GenericPostprocess"));
    auto *postprocessor = op->as<opk::op::OpInterfacePostprocessor>();
    ASSERT_NE(postprocessor, nullptr);
    EXPECT_TRUE(postprocessor->getProvidedContentTypes().empty());

    for (const auto &[parser, contentType] : parserContentTypes) {
        opk::AttributeMap attributes;
        attributes.set("parser", parser);
        ASSERT_TRUE(op->configure(attributes)) << parser;

        const auto expected = contentType == std::string_view{}
                                  ? std::vector<std::string_view>{}
                                  : std::vector<std::string_view>{contentType};
        EXPECT_EQ(postprocessor->getProvidedContentTypes(), expected) << parser;
    }
}

TEST(StandardContentTypes, InferenceControllerAdvertisesOptionalRequirement) {
    opk::op::OpRef op;
    ASSERT_TRUE(op.bind(OPK_STDOPS_PATH, "InferenceController"));
    auto *consumer = op->as<opk::op::OpInterfaceContentConsumer>();
    ASSERT_NE(consumer, nullptr);

    opk::AttributeMap attributes;
    ASSERT_TRUE(op->configure(attributes));
    EXPECT_TRUE(consumer->getRequiredContentTypes().empty());

    attributes.set("contentType", "humanFace");
    ASSERT_TRUE(op->configure(attributes));
    EXPECT_EQ(consumer->getRequiredContentTypes(), std::vector<std::string_view>{"humanFace"});
}

TEST(StandardContentTypes, OpChainCollectsUniqueNonEmptyContentTypes) {
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
    chain.add(yolo);
    chain.add(yoloX);
    chain.add(dummy);
    chain.add(controller);
    ASSERT_TRUE(chain.bind());

    EXPECT_EQ(chain.getProvidedContentTypes(), std::vector<std::string_view>{"genericObject"});
    EXPECT_EQ(chain.getRequiredContentTypes(), std::vector<std::string_view>{"humanFace"});
}

} // namespace
