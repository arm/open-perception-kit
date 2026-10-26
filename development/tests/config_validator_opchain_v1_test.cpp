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

#include "config_validator_test_support.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <limits>

namespace {

using opk::config::test::hasRule;

opk::op::OpChainDescriptor builtInStage() {
    opk::op::OpChainDescriptor descriptor;
    descriptor.ops = {
        {"opk-std-ops/InferenceController", std::nullopt, {}},
        {"opk-std-ops/GenericImagePreprocess", std::nullopt, {}},
        {"opk-future-ops/Inference", std::nullopt, {}},
        {"opk-std-ops/GenericPostprocess", std::nullopt, {}},
    };
    return descriptor;
}

TEST(ConfigValidator, SileroAudioUsesOpChain12Contract) {
    const auto supported = opk::config::supportedOpChainVersion();
    ASSERT_TRUE(supported) << supported.error().toText();
    EXPECT_EQ(*supported, "1.3.0");

    const auto path =
        std::filesystem::path{OPK_REPOSITORY_ROOT} / "config/models/silero-vad/opchain.json";
    std::ifstream input(path);
    ASSERT_TRUE(input);
    const auto document = nlohmann::json::parse(input);
    EXPECT_EQ(document.at("version"), "1.2.0");

    const auto result = opk::config::validateOpChainJson(document.dump(), path.string());
    ASSERT_TRUE(result) << result.error().toText();
    EXPECT_EQ(nlohmann::json(*result).at("version"), document.at("version"));
}

TEST(ConfigValidator, SmartTurnUsesOpChain13Contract) {
    for (const auto *filename : {"opchain.json", "opchain-vad.json"}) {
        const auto path =
            std::filesystem::path{OPK_REPOSITORY_ROOT} / "config/models/smart-turn" / filename;
        std::ifstream input(path);
        ASSERT_TRUE(input);
        auto document = nlohmann::json::parse(input);
        EXPECT_EQ(document.at("version"), "1.3.0");
        const auto result = opk::config::validateOpChainJson(document.dump(), path.string());
        ASSERT_TRUE(result) << result.error().toText();
        EXPECT_EQ(nlohmann::json(*result).at("version"), "1.3.0");

        auto invalid = document;
        invalid["ops"].back()["attributes"]["threshold"] = 1.1;
        EXPECT_FALSE(opk::config::validateOpChainJson(invalid.dump()));
        document["version"] = "2.0.0";
        EXPECT_FALSE(opk::config::validateOpChainJson(document.dump()));
    }
}

TEST(ConfigValidator, RejectsInvalidTurnDetectorAttributesAndLoops) {
    const auto path =
        std::filesystem::path{OPK_REPOSITORY_ROOT} / "config/models/smart-turn/opchain-vad.json";
    std::ifstream input(path);
    ASSERT_TRUE(input);
    const auto document = nlohmann::json::parse(input);
    for (const auto *attribute : {"hopSampleCount", "retryMs", "maxAttempts"}) {
        auto invalid = document;
        invalid["ops"][3]["attributes"][attribute] = 0;
        EXPECT_FALSE(opk::config::validateOpChainJson(invalid.dump())) << attribute;
    }
    auto invalid = document;
    invalid["ops"][3]["attributes"]["silenceMs"] = -1;
    EXPECT_FALSE(opk::config::validateOpChainJson(invalid.dump()));
    invalid = document;
    invalid["ops"][3]["attributes"]["unknown"] = true;
    EXPECT_FALSE(opk::config::validateOpChainJson(invalid.dump()));
    invalid = document;
    for (auto &op : invalid["ops"])
        op["loopId"] = 1;
    EXPECT_FALSE(opk::config::validateOpChainJson(invalid.dump()));
}

TEST(ConfigValidator, OpChainModelDescriptorAcceptsFilesystemPathsOnly) {
    using namespace std::string_literals;

    nlohmann::json document{{"version", "1.0.0"},
                            {"name", "model-reference"},
                            {"description", "Validate the model descriptor path contract."},
                            {"ops",
                             {{{"id", "opk-future-ops/Inference"},
                               {"attributes", {{"modelDescriptor", "model.json"}}}}}}};

    for (const auto *path : {"../models/model.json", "/opt/models/model.json"}) {
        document["ops"][0]["attributes"]["modelDescriptor"] = path;
        const auto result = opk::config::validateOpChainJson(document.dump());
        EXPECT_TRUE(result) << path << '\n' << (result ? "" : result.error().toText());
    }

    for (const auto *path : {"hf:Arm/example#file=model.json", "https://example.com/model.json"}) {
        document["ops"][0]["attributes"]["modelDescriptor"] = path;
        const auto result = opk::config::validateOpChainJson(document.dump());
        ASSERT_FALSE(result) << path;
        EXPECT_TRUE(hasRule(result.error(), "schema.validation")) << path;
    }

    document["ops"][0]["attributes"] = nlohmann::json::object();
    EXPECT_FALSE(opk::config::validateOpChainJson(document.dump()));
    document["ops"][0]["attributes"] = {{"modelDescriptor", "model.json"}, {"extra", true}};
    EXPECT_FALSE(opk::config::validateOpChainJson(document.dump()));

    constexpr int AutomaticThreads = 0;
    constexpr int ExplicitThreads = 2;
    constexpr int MaximumThreads = std::numeric_limits<int>::max();
    document["ops"][0] = {{"id", "opk-onnx-ops/Inference"},
                          {"attributes", {{"modelDescriptor", "model.json"}}}};
    EXPECT_TRUE(opk::config::validateOpChainJson(document.dump()));
    for (const int threads : {AutomaticThreads, ExplicitThreads, MaximumThreads}) {
        document["ops"][0]["attributes"]["intraOpNumThreads"] = threads;
        const auto result = opk::config::validateOpChainJson(document.dump());
        EXPECT_TRUE(result) << threads << '\n' << (result ? "" : result.error().toText());
    }
    const std::array invalidThreads{
        "-1"s, "1.5"s, "2147483648"s, "2.0"s, "2e0"s, "true"s, R"("2")"s, "null"s};
    for (const auto &threads : invalidThreads) {
        const std::string json =
            R"({"version":"1.1.0","name":"thread-notation","description":"Validate thread notation.",
                "ops":[{"id":"opk-onnx-ops/Inference","attributes":{
                "modelDescriptor":"model.json","intraOpNumThreads":)" +
            threads + "}}]}";
        const auto result = opk::config::validateOpChainJson(json);
        ASSERT_FALSE(result) << threads;
    }
    document["ops"][0]["attributes"]["intraOpNumThreads"] = ExplicitThreads;
    for (const auto *backend : {"opk-executorch-ops/Inference", "opk-future-ops/Inference"}) {
        document["ops"][0]["id"] = backend;
        EXPECT_FALSE(opk::config::validateOpChainJson(document.dump())) << backend;
    }

    document["ops"][0] = {{"id", "custom/InferenceLike"},
                          {"attributes", {{"implementationDefined", true}}}};
    const auto custom = opk::config::validateOpChainJson(document.dump());
    EXPECT_TRUE(custom) << (custom ? "" : custom.error().toText());
}

TEST(ConfigValidator, OpChainAttributesAreOptionalExceptForDataBearingOps) {
    nlohmann::json document{
        {"version", "1.0.0"},
        {"name", "optional-attributes"},
        {"description", "Validate optional empty operation attributes."},
        {"ops",
         {{{"id", "opk-std-ops/InferenceController"}},
          {{"id", "opk-std-ops/GenericImagePreprocess"}},
          {{"id", "opk-future-ops/Inference"}, {"attributes", {{"modelDescriptor", "model.json"}}}},
          {{"id", "custom/Operation"}},
          {{"id", "opk-std-ops/GenericPostprocess"}, {"attributes", {{"parser", "DummyParser"}}}}}},
    };

    const auto result = opk::config::validateOpChainJson(document.dump());
    ASSERT_TRUE(result) << (result ? "" : result.error().toText());
    for (const auto index : {0U, 1U, 3U})
        EXPECT_TRUE(result->ops[index].attributes.raw().empty());

    for (const auto *id : {"opk-future-ops/Inference", "opk-std-ops/GenericPostprocess"}) {
        document["ops"] = {{{"id", id}}};
        const auto missing = opk::config::validateOpChainJson(document.dump());
        ASSERT_FALSE(missing) << id;
        EXPECT_TRUE(hasRule(missing.error(), "schema.validation")) << id;
    }
}

TEST(ConfigValidator, OpChainAcceptsStableInstanceIds) {
    nlohmann::json document{
        {"version", "1.0.0"},
        {"name", "instance-id"},
        {"description", "Validate operation producer identities."},
        {"ops", {{{"id", "custom/Operation"}, {"instanceId", "python-classifier"}}}},
    };

    const auto result = opk::config::validateOpChainJson(document.dump());
    ASSERT_TRUE(result) << (result ? "" : result.error().toText());
    EXPECT_EQ(result->ops[0].instanceId, "python-classifier");

    document["ops"][0]["instanceId"] = "invalid instance";
    EXPECT_FALSE(opk::config::validateOpChainJson(document.dump()));

    document["ops"] = {
        {{"id", "custom/First"}, {"instanceId", "duplicate"}},
        {{"id", "custom/Second"}, {"instanceId", "duplicate"}},
    };
    const auto duplicate = opk::config::validateOpChainJson(document.dump());
    ASSERT_FALSE(duplicate);
    EXPECT_TRUE(hasRule(duplicate.error(), "opchain.v1.instance-id"));
}

TEST(ConfigValidator, OpChainValidatesPythonScriptAttributes) {
    nlohmann::json document{
        {"version", "1.0.0"},
        {"name", "python-script"},
        {"description", "Validate PythonScript attributes."},
        {"ops",
         {{{"id", "opk-python-ops/PythonScript"},
           {"attributes",
            {{"script", "scripts/process.py"}, {"pythonPaths", {"scripts/modules"}}}}}}},
    };

    EXPECT_TRUE(opk::config::validateOpChainJson(document.dump()));

    document["ops"][0]["attributes"].erase("script");
    EXPECT_FALSE(opk::config::validateOpChainJson(document.dump()));

    document["ops"][0]["attributes"] = {{"script", "scripts/process.py"},
                                        {"pythonPaths", "scripts/modules"}};
    EXPECT_FALSE(opk::config::validateOpChainJson(document.dump()));
}

TEST(ConfigValidator, OpChainProjectionClearsOmittedAttributes) {
    opk::op::OpChainDescriptor::Op reused;
    reused.attributes.set("stale", true);
    nlohmann::json{{"id", "custom/Operation"}}.get_to(reused);
    EXPECT_TRUE(reused.attributes.raw().empty());
}

TEST(ConfigValidator, OpChainSchemaValidatesRegisteredParserContracts) {
    const auto supportedVersion = opk::config::supportedOpChainVersion();
    ASSERT_TRUE(supportedVersion) << supportedVersion.error().toText();
    EXPECT_EQ(*supportedVersion, "1.2.0");

    nlohmann::json document{
        {"version", "1.2.0"},
        {"name", "parser-contract"},
        {"description", "Validate GenericPostprocess parser attributes."},
        {"ops",
         {{{"id", "opk-std-ops/GenericPostprocess"}, {"attributes", {{"parser", "DummyParser"}}}}}},
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
        const auto result = opk::config::validateOpChainJson(document.dump());
        EXPECT_TRUE(result) << parser << '\n' << (result ? "" : result.error().toText());
    }

    const auto validConditionalAttributes = std::array{
        nlohmann::json{{"parser", "CameraContactParser"},
                       {"contactClassIndex", 0},
                       {"noContactClassIndex", 1}},
        nlohmann::json{{"parser", "GazeDetectionParser"}, {"angleBinWidthDeg", 4}},
        nlohmann::json{{"parser", "YoloParser"}, {"applyNms", false}},
        nlohmann::json{{"parser", "YoloParser"}, {"outputFormat", "cornerScoreClass"}},
        nlohmann::json{{"parser", "YoloParser"},
                       {"outputFormat", "cornerClassScores"},
                       {"maxDetections", 300}},
        nlohmann::json{{"parser", "YoloXParser"}, {"applyNms", false}},
    };
    for (const auto &validAttributes : validConditionalAttributes) {
        attributes = validAttributes;
        const auto result = opk::config::validateOpChainJson(document.dump());
        EXPECT_TRUE(result) << validAttributes.dump() << '\n'
                            << (result ? "" : result.error().toText());
    }

    const auto invalidAttributes = std::array{
        nlohmann::json{{"parser", "UnknownParser"}},
        nlohmann::json{{"parser", "GazeDetectionParser"}, {"unexpected", true}},
        nlohmann::json{{"parser", "GazeDetectionParser"}, {"angleBinWidthDeg", 0}},
        nlohmann::json{{"parser", "CameraContactParser"},
                       {"contactClassIndex", 0},
                       {"noContactClassIndex", 0}},
        nlohmann::json{{"parser", "PaddleOcrDetectionParser"}, {"gamma", 0}},
        nlohmann::json{{"parser", "YoloParser"}, {"applyNms", false}, {"iouThreshold", 0.4}},
        nlohmann::json{{"parser", "YoloParser"}, {"outputFormat", "xyxy"}},
        nlohmann::json{
            {"parser", "YoloParser"}, {"outputFormat", "cornerClassScores"}, {"maxDetections", 0}},
        nlohmann::json{{"parser", "YoloXParser"}, {"applyNms", false}, {"iouThreshold", 0.4}},
    };
    for (const auto &invalid : invalidAttributes) {
        attributes = invalid;
        const auto result = opk::config::validateOpChainJson(document.dump());
        ASSERT_FALSE(result) << invalid.dump();
        EXPECT_TRUE(hasRule(result.error(), "schema.validation")) << invalid.dump();
    }
}

TEST(ConfigValidator, OpChainV1RejectsControlsButAllowsUnicodeInOpId) {
    auto document = nlohmann::json{
        {"version", "1.0.0"},
        {"name", "op-id-validation"},
        {"description", "Validate operation identifiers."},
        {"ops", {{{"id", "custom/Operation"}, {"attributes", nlohmann::json::object()}}}}};

    document["ops"][0]["id"] = std::string{"custom/"} + "\xC2\x80" + "Operation";
    const auto controlResult = opk::config::validateOpChainJson(document.dump());
    ASSERT_FALSE(controlResult);
    EXPECT_TRUE(hasRule(controlResult.error(), "opchain.v1.op-id-control"));

    document["ops"][0]["id"] = std::string{"custom/"} + "\xC3\x81" + "Operation";
    const auto unicodeResult = opk::config::validateOpChainJson(document.dump());
    EXPECT_TRUE(unicodeResult) << (unicodeResult ? "" : unicodeResult.error().toText());
}

TEST(ConfigValidator, OpChainV1ValidatesLoopGroups) {
    opk::op::OpChainDescriptor descriptor;
    descriptor.ops = {
        {"custom/A", 1, {}},
        {"custom/B", 1, {}},
    };
    EXPECT_TRUE(opk::config::validateOpChainSemantics(descriptor).ok());

    descriptor.ops.pop_back();
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v1.loop-group"));

    descriptor.ops[0].loopId = 0;
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v1.loop-group"));

    descriptor.ops = {
        {"custom/A", 1, {}},
        {"custom/B", std::nullopt, {}},
        {"custom/C", 1, {}},
    };
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v1.loop-group"));
}

TEST(ConfigValidator, OpChainV1ValidatesBuiltInStageStructure) {
    auto descriptor = builtInStage();
    EXPECT_TRUE(opk::config::validateOpChainSemantics(descriptor).ok());

    descriptor.ops.back().id = "opk-python-ops/PythonScript";
    EXPECT_TRUE(opk::config::validateOpChainSemantics(descriptor).ok());

    descriptor = builtInStage();
    descriptor.ops.insert(descriptor.ops.end() - 1,
                          {"opk-python-ops/PythonScript", std::nullopt, {}});
    EXPECT_TRUE(opk::config::validateOpChainSemantics(descriptor).ok());

    descriptor.ops.insert(descriptor.ops.end() - 1, {"custom/Between", std::nullopt, {}});
    EXPECT_TRUE(opk::config::validateOpChainSemantics(descriptor).ok());

    descriptor = builtInStage();
    descriptor.ops[1].id = "custom/WrongPreprocess";
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v1.builtin-stage"));

    descriptor = builtInStage();
    descriptor.ops[2].id = "custom/WrongInference";
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v1.builtin-stage"));

    descriptor = builtInStage();
    descriptor.ops.back().id = "custom/WrongPostprocess";
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v1.builtin-stage"));

    descriptor = builtInStage();
    descriptor.ops.back().id = "opk-python-ops/PythonScript";
    descriptor.ops.insert(descriptor.ops.end() - 1,
                          {"opk-std-ops/GenericPostprocess", std::nullopt, {}});
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v1.builtin-stage"));

    descriptor = builtInStage();
    descriptor.ops.insert(descriptor.ops.end() - 1,
                          {"opk-std-ops/GenericImagePreprocess", std::nullopt, {}});
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v1.builtin-stage"));

    descriptor.ops = {{"opk-std-ops/GenericImagePreprocess", std::nullopt, {}}};
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v1.builtin-stage"));
}

TEST(ConfigValidator, OpChainV1ValidatesBuiltInStageLoopOwnership) {
    auto descriptor = builtInStage();
    for (auto &op : descriptor.ops)
        op.loopId = 1;
    EXPECT_TRUE(opk::config::validateOpChainSemantics(descriptor).ok());

    descriptor.ops.insert(descriptor.ops.begin(), {"custom/Before", 1, {}});
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v1.stage-loop"));

    descriptor = builtInStage();
    for (auto &op : descriptor.ops)
        op.loopId = 1;
    descriptor.ops.back().loopId.reset();
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v1.stage-loop"));

    descriptor = builtInStage();
    descriptor.ops.front().attributes.set("contentType", "genericObject");
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v1.stage-loop"));
}

TEST(ConfigValidator, OpChainV1ValidatesEffectiveThresholdOrder) {
    opk::op::OpChainDescriptor descriptor;
    descriptor.ops = {{"opk-std-ops/GenericPostprocess", std::nullopt, {}}};
    auto &attributes = descriptor.ops.front().attributes;
    attributes.set("parser", "ModNetSegmentationParser");
    EXPECT_TRUE(opk::config::validateOpChainSemantics(descriptor).ok());

    attributes.set("thresholdLow", 0.9);
    attributes.set("thresholdHigh", 0.8);
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v1.threshold-order"));

    attributes.set("parser", "PaddleOcrDetectionParser");
    attributes.set("thresholdLow", 0.6);
    attributes.set("thresholdHigh", 0.8);
    EXPECT_TRUE(opk::config::validateOpChainSemantics(descriptor).ok());
}

} // namespace
