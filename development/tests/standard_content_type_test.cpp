/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

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

    pek::op::OpRef op;
    ASSERT_TRUE(op.bind(PEK_STDOPS_PATH, "GenericPostprocess"));
    auto *postprocessor = op->as<pek::op::OpInterfacePostprocessor>();
    ASSERT_NE(postprocessor, nullptr);
    EXPECT_TRUE(postprocessor->getProvidedContentTypes().empty());

    for (const auto &[parser, contentType] : parserContentTypes) {
        pek::AttributeMap attributes;
        attributes.set("parser", parser);
        ASSERT_TRUE(op->configure(attributes)) << parser;

        const auto expected = contentType == std::string_view{}
                                  ? std::vector<std::string_view>{}
                                  : std::vector<std::string_view>{contentType};
        EXPECT_EQ(postprocessor->getProvidedContentTypes(), expected) << parser;
    }
}

TEST(StandardContentTypes, InferenceControllerAdvertisesOptionalRequirement) {
    pek::op::OpRef op;
    ASSERT_TRUE(op.bind(PEK_STDOPS_PATH, "InferenceController"));
    auto *consumer = op->as<pek::op::OpInterfaceContentConsumer>();
    ASSERT_NE(consumer, nullptr);

    pek::AttributeMap attributes;
    ASSERT_TRUE(op->configure(attributes));
    EXPECT_TRUE(consumer->getRequiredContentTypes().empty());

    attributes.set("contentType", "humanFace");
    ASSERT_TRUE(op->configure(attributes));
    EXPECT_EQ(consumer->getRequiredContentTypes(), std::vector<std::string_view>{"humanFace"});
}

TEST(StandardContentTypes, OpChainCollectsUniqueNonEmptyContentTypes) {
    pek::op::OpRef yolo;
    pek::op::OpRef yoloX;
    pek::op::OpRef dummy;
    pek::op::OpRef controller;
    ASSERT_TRUE(yolo.bind(PEK_STDOPS_PATH, "GenericPostprocess"));
    ASSERT_TRUE(yoloX.bind(PEK_STDOPS_PATH, "GenericPostprocess"));
    ASSERT_TRUE(dummy.bind(PEK_STDOPS_PATH, "GenericPostprocess"));
    ASSERT_TRUE(controller.bind(PEK_STDOPS_PATH, "InferenceController"));

    pek::AttributeMap attributes;
    ASSERT_TRUE(yolo->configure(attributes.set("parser", "YoloParser")));
    ASSERT_TRUE(yoloX->configure(attributes.set("parser", "YoloXParser")));
    ASSERT_TRUE(dummy->configure(attributes.set("parser", "DummyParser")));
    attributes.clear();
    ASSERT_TRUE(controller->configure(attributes.set("contentType", "humanFace")));

    pek::op::OpChain chain;
    chain.add(yolo);
    chain.add(yoloX);
    chain.add(dummy);
    chain.add(controller);
    ASSERT_TRUE(chain.bind());

    EXPECT_EQ(chain.getProvidedContentTypes(), std::vector<std::string_view>{"genericObject"});
    EXPECT_EQ(chain.getRequiredContentTypes(), std::vector<std::string_view>{"humanFace"});
}

} // namespace
