/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "config_validator_test_support.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <array>

namespace {

using pek::config::test::hasRule;

pek::op::OpChainDescriptor builtInStage() {
    pek::op::OpChainDescriptor descriptor;
    descriptor.ops = {
        {"pek-std-ops/InferenceController", std::nullopt, {}},
        {"pek-std-ops/GenericImagePreprocess", std::nullopt, {}},
        {"pek-future-ops/Inference", std::nullopt, {}},
        {"pek-std-ops/GenericPostprocess", std::nullopt, {}},
    };
    return descriptor;
}

TEST(ConfigValidator, OpChainModelDescriptorAcceptsFilesystemPathsOnly) {
    nlohmann::json document{{"version", 1},
                            {"name", "model-reference"},
                            {"description", "Validate the model descriptor path contract."},
                            {"ops",
                             {{{"id", "pek-future-ops/Inference"},
                               {"attributes", {{"modelDescriptor", "model.json"}}}}}}};

    for (const auto *path : {"../models/model.json", "/opt/models/model.json"}) {
        document["ops"][0]["attributes"]["modelDescriptor"] = path;
        const auto result = pek::config::validateOpChainJson(document.dump());
        EXPECT_TRUE(result) << path << '\n' << (result ? "" : result.error().toText());
    }

    for (const auto *path : {"hf:Arm/example#file=model.json", "https://example.com/model.json"}) {
        document["ops"][0]["attributes"]["modelDescriptor"] = path;
        const auto result = pek::config::validateOpChainJson(document.dump());
        ASSERT_FALSE(result) << path;
        EXPECT_TRUE(hasRule(result.error(), "schema.validation")) << path;
    }

    document["ops"][0]["attributes"] = nlohmann::json::object();
    EXPECT_FALSE(pek::config::validateOpChainJson(document.dump()));
    document["ops"][0]["attributes"] = {{"modelDescriptor", "model.json"}, {"extra", true}};
    EXPECT_FALSE(pek::config::validateOpChainJson(document.dump()));

    document["ops"][0] = {{"id", "custom/InferenceLike"},
                          {"attributes", {{"implementationDefined", true}}}};
    const auto custom = pek::config::validateOpChainJson(document.dump());
    EXPECT_TRUE(custom) << (custom ? "" : custom.error().toText());
}

TEST(ConfigValidator, OpChainAttributesAreOptionalExceptForDataBearingOps) {
    nlohmann::json document{
        {"version", 1},
        {"name", "optional-attributes"},
        {"description", "Validate optional empty operation attributes."},
        {"ops",
         {{{"id", "pek-std-ops/InferenceController"}},
          {{"id", "pek-std-ops/GenericImagePreprocess"}},
          {{"id", "pek-future-ops/Inference"}, {"attributes", {{"modelDescriptor", "model.json"}}}},
          {{"id", "custom/Operation"}},
          {{"id", "pek-std-ops/GenericPostprocess"}, {"attributes", {{"parser", "DummyParser"}}}}}},
    };

    const auto result = pek::config::validateOpChainJson(document.dump());
    ASSERT_TRUE(result) << (result ? "" : result.error().toText());
    for (const auto index : {0U, 1U, 3U})
        EXPECT_TRUE(result->ops[index].attributes.raw().empty());

    for (const auto *id : {"pek-future-ops/Inference", "pek-std-ops/GenericPostprocess"}) {
        document["ops"] = {{{"id", id}}};
        const auto missing = pek::config::validateOpChainJson(document.dump());
        ASSERT_FALSE(missing) << id;
        EXPECT_TRUE(hasRule(missing.error(), "schema.validation")) << id;
    }
}

TEST(ConfigValidator, OpChainAcceptsStableInstanceIds) {
    nlohmann::json document{
        {"version", 1},
        {"name", "instance-id"},
        {"description", "Validate operation producer identities."},
        {"ops", {{{"id", "custom/Operation"}, {"instanceId", "python-classifier"}}}},
    };

    const auto result = pek::config::validateOpChainJson(document.dump());
    ASSERT_TRUE(result) << (result ? "" : result.error().toText());
    EXPECT_EQ(result->ops[0].instanceId, "python-classifier");

    document["ops"][0]["instanceId"] = "invalid instance";
    EXPECT_FALSE(pek::config::validateOpChainJson(document.dump()));

    document["ops"] = {
        {{"id", "custom/First"}, {"instanceId", "duplicate"}},
        {{"id", "custom/Second"}, {"instanceId", "duplicate"}},
    };
    const auto duplicate = pek::config::validateOpChainJson(document.dump());
    ASSERT_FALSE(duplicate);
    EXPECT_TRUE(hasRule(duplicate.error(), "opchain.v1.instance-id"));
}

TEST(ConfigValidator, OpChainProjectionClearsOmittedAttributes) {
    pek::op::OpChainDescriptor::Op reused;
    reused.attributes.set("stale", true);
    nlohmann::json{{"id", "custom/Operation"}}.get_to(reused);
    EXPECT_TRUE(reused.attributes.raw().empty());
}

TEST(ConfigValidator, OpChainSchemaValidatesRegisteredParserContracts) {
    nlohmann::json document{
        {"version", 1},
        {"name", "parser-contract"},
        {"description", "Validate GenericPostprocess parser attributes."},
        {"ops",
         {{{"id", "pek-std-ops/GenericPostprocess"}, {"attributes", {{"parser", "DummyParser"}}}}}},
    };
    auto &attributes = document["ops"][0]["attributes"];
    constexpr std::array Parsers = {
        "CameraContactParser",
        "DummyParser",
        "GazeDetectionParser",
        "ImageNetClassificationParser",
        "ModNetSegmentationParser",
        "ObjectEmbeddingParser",
        "PaddleOcrDetectionParser",
        "PersonClassificationParser",
        "RvmParser",
        "ScrfdParser",
        "UltrafaceParser",
        "YoloParser",
        "YoloXParser",
    };

    for (const auto *parser : Parsers) {
        attributes = {{"parser", parser}};
        const auto result = pek::config::validateOpChainJson(document.dump());
        EXPECT_TRUE(result) << parser << '\n' << (result ? "" : result.error().toText());
    }

    const auto validConditionalAttributes = std::array{
        nlohmann::json{{"parser", "CameraContactParser"},
                       {"contactClassIndex", 0},
                       {"noContactClassIndex", 1}},
        nlohmann::json{{"parser", "YoloParser"}, {"applyNms", false}},
        nlohmann::json{{"parser", "YoloXParser"}, {"applyNms", false}},
    };
    for (const auto &validAttributes : validConditionalAttributes) {
        attributes = validAttributes;
        const auto result = pek::config::validateOpChainJson(document.dump());
        EXPECT_TRUE(result) << validAttributes.dump() << '\n'
                            << (result ? "" : result.error().toText());
    }

    const auto invalidAttributes = std::array{
        nlohmann::json{{"parser", "UnknownParser"}},
        nlohmann::json{{"parser", "GazeDetectionParser"}, {"unexpected", true}},
        nlohmann::json{{"parser", "CameraContactParser"},
                       {"contactClassIndex", 0},
                       {"noContactClassIndex", 0}},
        nlohmann::json{{"parser", "PaddleOcrDetectionParser"}, {"gamma", 0}},
        nlohmann::json{{"parser", "YoloParser"}, {"applyNms", false}, {"iouThreshold", 0.4}},
        nlohmann::json{{"parser", "YoloXParser"}, {"applyNms", false}, {"iouThreshold", 0.4}},
    };
    for (const auto &invalid : invalidAttributes) {
        attributes = invalid;
        const auto result = pek::config::validateOpChainJson(document.dump());
        ASSERT_FALSE(result) << invalid.dump();
        EXPECT_TRUE(hasRule(result.error(), "schema.validation")) << invalid.dump();
    }
}

TEST(ConfigValidator, OpChainV1RejectsControlsButAllowsUnicodeInOpId) {
    auto document = nlohmann::json{
        {"version", 1},
        {"name", "op-id-validation"},
        {"description", "Validate operation identifiers."},
        {"ops", {{{"id", "custom/Operation"}, {"attributes", nlohmann::json::object()}}}}};

    document["ops"][0]["id"] = std::string{"custom/"} + "\xC2\x80" + "Operation";
    const auto controlResult = pek::config::validateOpChainJson(document.dump());
    ASSERT_FALSE(controlResult);
    EXPECT_TRUE(hasRule(controlResult.error(), "opchain.v1.op-id-control"));

    document["ops"][0]["id"] = std::string{"custom/"} + "\xC3\x81" + "Operation";
    const auto unicodeResult = pek::config::validateOpChainJson(document.dump());
    EXPECT_TRUE(unicodeResult) << (unicodeResult ? "" : unicodeResult.error().toText());
}

TEST(ConfigValidator, OpChainV1ValidatesLoopGroups) {
    pek::op::OpChainDescriptor descriptor;
    descriptor.ops = {
        {"custom/A", 1, {}},
        {"custom/B", 1, {}},
    };
    EXPECT_TRUE(pek::config::validateOpChainSemantics(descriptor).ok());

    descriptor.ops.pop_back();
    EXPECT_TRUE(
        hasRule(pek::config::validateOpChainSemantics(descriptor), "opchain.v1.loop-group"));

    descriptor.ops[0].loopId = 0;
    EXPECT_TRUE(
        hasRule(pek::config::validateOpChainSemantics(descriptor), "opchain.v1.loop-group"));

    descriptor.ops = {
        {"custom/A", 1, {}},
        {"custom/B", std::nullopt, {}},
        {"custom/C", 1, {}},
    };
    EXPECT_TRUE(
        hasRule(pek::config::validateOpChainSemantics(descriptor), "opchain.v1.loop-group"));
}

TEST(ConfigValidator, OpChainV1ValidatesBuiltInStageStructure) {
    auto descriptor = builtInStage();
    EXPECT_TRUE(pek::config::validateOpChainSemantics(descriptor).ok());

    descriptor.ops.insert(descriptor.ops.end() - 1, {"custom/Between", std::nullopt, {}});
    EXPECT_TRUE(pek::config::validateOpChainSemantics(descriptor).ok());

    descriptor = builtInStage();
    descriptor.ops[1].id = "custom/WrongPreprocess";
    EXPECT_TRUE(
        hasRule(pek::config::validateOpChainSemantics(descriptor), "opchain.v1.builtin-stage"));

    descriptor = builtInStage();
    descriptor.ops[2].id = "custom/WrongInference";
    EXPECT_TRUE(
        hasRule(pek::config::validateOpChainSemantics(descriptor), "opchain.v1.builtin-stage"));

    descriptor = builtInStage();
    descriptor.ops.back().id = "custom/WrongPostprocess";
    EXPECT_TRUE(
        hasRule(pek::config::validateOpChainSemantics(descriptor), "opchain.v1.builtin-stage"));

    descriptor = builtInStage();
    descriptor.ops.insert(descriptor.ops.end() - 1,
                          {"pek-std-ops/GenericImagePreprocess", std::nullopt, {}});
    EXPECT_TRUE(
        hasRule(pek::config::validateOpChainSemantics(descriptor), "opchain.v1.builtin-stage"));

    descriptor.ops = {{"pek-std-ops/GenericImagePreprocess", std::nullopt, {}}};
    EXPECT_TRUE(
        hasRule(pek::config::validateOpChainSemantics(descriptor), "opchain.v1.builtin-stage"));
}

TEST(ConfigValidator, OpChainV1ValidatesBuiltInStageLoopOwnership) {
    auto descriptor = builtInStage();
    for (auto &op : descriptor.ops)
        op.loopId = 1;
    EXPECT_TRUE(pek::config::validateOpChainSemantics(descriptor).ok());

    descriptor.ops.insert(descriptor.ops.begin(), {"custom/Before", 1, {}});
    EXPECT_TRUE(
        hasRule(pek::config::validateOpChainSemantics(descriptor), "opchain.v1.stage-loop"));

    descriptor = builtInStage();
    for (auto &op : descriptor.ops)
        op.loopId = 1;
    descriptor.ops.back().loopId.reset();
    EXPECT_TRUE(
        hasRule(pek::config::validateOpChainSemantics(descriptor), "opchain.v1.stage-loop"));

    descriptor = builtInStage();
    descriptor.ops.front().attributes.set("contentType", "genericObject");
    EXPECT_TRUE(
        hasRule(pek::config::validateOpChainSemantics(descriptor), "opchain.v1.stage-loop"));
}

TEST(ConfigValidator, OpChainV1ValidatesEffectiveThresholdOrder) {
    pek::op::OpChainDescriptor descriptor;
    descriptor.ops = {{"pek-std-ops/GenericPostprocess", std::nullopt, {}}};
    auto &attributes = descriptor.ops.front().attributes;
    attributes.set("parser", "ModNetSegmentationParser");
    EXPECT_TRUE(pek::config::validateOpChainSemantics(descriptor).ok());

    attributes.set("thresholdLow", 0.9);
    attributes.set("thresholdHigh", 0.8);
    EXPECT_TRUE(
        hasRule(pek::config::validateOpChainSemantics(descriptor), "opchain.v1.threshold-order"));

    attributes.set("parser", "PaddleOcrDetectionParser");
    attributes.set("thresholdLow", 0.6);
    attributes.set("thresholdHigh", 0.8);
    EXPECT_TRUE(pek::config::validateOpChainSemantics(descriptor).ok());
}

} // namespace
