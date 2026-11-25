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
        {"opk-std-ops/InferenceController", std::nullopt, {}, {}, opk::op::StageRole::Controller},
        {"opk-std-ops/GenericImagePreprocess",
         std::nullopt,
         {},
         {},
         opk::op::StageRole::ImagePreprocess},
        {"opk-future-ops/Inference", std::nullopt, {}, {}, opk::op::StageRole::Inference},
        {"opk-std-ops/GenericPostprocess", std::nullopt, {}, {}, opk::op::StageRole::Postprocess},
    };
    return descriptor;
}

TEST(ConfigValidator, SileroAudioPreservesOpChainVersion) {
    const auto supported = opk::config::supportedOpChainVersion();
    ASSERT_TRUE(supported) << supported.error().toText();
    EXPECT_EQ(*supported, "3.0.0");

    const auto path =
        std::filesystem::path{OPK_REPOSITORY_ROOT} / "config/models/silero-vad/opchain.json";
    std::ifstream input(path);
    ASSERT_TRUE(input);
    const auto document = nlohmann::json::parse(input);

    const auto result = opk::config::validateOpChainJson(document.dump(), path.string());
    ASSERT_TRUE(result) << result.error().toText();
    EXPECT_EQ(nlohmann::json(*result).at("version"), document.at("version"));
}

TEST(ConfigValidator, YoloClassScoresUseCurrentOpChainContract) {
    const auto path =
        std::filesystem::path{OPK_REPOSITORY_ROOT} / "config/models/yolo26n-320-mlwl/opchain.json";
    std::ifstream input(path);
    ASSERT_TRUE(input);
    const auto document = nlohmann::json::parse(input);
    EXPECT_EQ(document.at("version"), "3.0.0");
    const auto result = opk::config::validateOpChainJson(document.dump(), path.string());
    ASSERT_TRUE(result) << result.error().toText();
    EXPECT_EQ(nlohmann::json(*result).at("version"), document.at("version"));
}

TEST(ConfigValidator, SmartTurnPreservesOpChainVersion) {
    for (const auto *filename : {"opchain.json", "opchain-vad.json"}) {
        const auto path =
            std::filesystem::path{OPK_REPOSITORY_ROOT} / "config/models/smart-turn" / filename;
        std::ifstream input(path);
        ASSERT_TRUE(input);
        auto document = nlohmann::json::parse(input);
        const auto result = opk::config::validateOpChainJson(document.dump(), path.string());
        ASSERT_TRUE(result) << result.error().toText();
        EXPECT_EQ(nlohmann::json(*result).at("version"), document.at("version"));

        auto invalid = document;
        invalid["ops"].back()["attributes"]["threshold"] = 1.1;
        EXPECT_FALSE(opk::config::validateOpChainJson(invalid.dump()));
        document["version"] = "1.0.0";
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

    nlohmann::json document{{"version", "3.0.0"},
                            {"name", "model-reference"},
                            {"description", "Validate the model descriptor path contract."},
                            {"ops",
                             {{{"id", "opk-future-ops/Inference"},
                               {"role", "inference"},
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
    constexpr int SequentialThreads = 1;
    constexpr int ExplicitThreads = 2;
    constexpr int MaximumThreads = std::numeric_limits<int>::max();
    document["ops"][0] = {{"id", "opk-onnx-ops/Inference"},
                          {"role", "inference"},
                          {"attributes", {{"modelDescriptor", "model.json"}}}};
    EXPECT_TRUE(opk::config::validateOpChainJson(document.dump()));
    for (const auto *attribute : {"intraOpNumThreads", "interOpNumThreads"}) {
        document["ops"][0] = {{"id", "opk-onnx-ops/Inference"},
                              {"role", "inference"},
                              {"attributes", {{"modelDescriptor", "model.json"}}}};
        for (const int threads :
             {AutomaticThreads, SequentialThreads, ExplicitThreads, MaximumThreads}) {
            document["ops"][0]["attributes"][attribute] = threads;
            const auto result = opk::config::validateOpChainJson(document.dump());
            EXPECT_TRUE(result) << threads << '\n' << (result ? "" : result.error().toText());
        }
        const std::array invalidThreads{
            "-1"s, "1.5"s, "2147483648"s, "2.0"s, "2e0"s, "true"s, R"("2")"s, "null"s};
        for (const auto &threads : invalidThreads) {
            const std::string json =
                R"({"version":"3.0.0","name":"thread-notation","description":"Validate thread notation.",
                    "ops":[{"id":"opk-onnx-ops/Inference", "role": "inference","attributes":{
                    "modelDescriptor":"model.json",)" +
                std::string{"\""} + attribute + "\":" + threads + "}}]}";
            const auto result = opk::config::validateOpChainJson(json);
            ASSERT_FALSE(result) << attribute << ": " << threads;
        }
        document["ops"][0]["attributes"][attribute] = ExplicitThreads;
        for (const auto *backend : {"opk-executorch-ops/Inference", "opk-future-ops/Inference"}) {
            document["ops"][0]["id"] = backend;
            EXPECT_FALSE(opk::config::validateOpChainJson(document.dump())) << backend;
        }
    }

    document["ops"][0] = {{"id", "opk-onnx-ops/Inference"},
                          {"role", "inference"},
                          {"attributes",
                           {{"modelDescriptor", "model.json"},
                            {"intraOpNumThreads", ExplicitThreads},
                            {"interOpNumThreads", ExplicitThreads}}}};
    EXPECT_TRUE(opk::config::validateOpChainJson(document.dump()));

    document["ops"][0] = {{"id", "custom/InferenceLike"},
                          {"role", "intermediate-process"},
                          {"attributes", {{"implementationDefined", true}}}};
    const auto custom = opk::config::validateOpChainJson(document.dump());
    EXPECT_TRUE(custom) << (custom ? "" : custom.error().toText());
}

TEST(ConfigValidator, OpChainAttributesAreOptionalExceptForDataBearingOps) {
    nlohmann::json document{
        {"version", "3.0.0"},
        {"name", "optional-attributes"},
        {"description", "Validate optional empty operation attributes."},
        {"ops",
         {{{"id", "opk-std-ops/InferenceController"}, {"role", "controller"}},
          {{"id", "opk-std-ops/GenericImagePreprocess"}, {"role", "image-preprocess"}},
          {{"id", "opk-future-ops/Inference"},
           {"role", "inference"},
           {"attributes", {{"modelDescriptor", "model.json"}}}},
          {{"id", "custom/Operation"}, {"role", "intermediate-process"}},
          {{"id", "opk-std-ops/GenericPostprocess"},
           {"role", "postprocess"},
           {"attributes", {{"parser", "DummyParser"}}}}}},
    };

    const auto result = opk::config::validateOpChainJson(document.dump());
    ASSERT_TRUE(result) << (result ? "" : result.error().toText());
    for (const auto index : {0U, 1U, 3U})
        EXPECT_TRUE(result->ops[index].attributes.raw().empty());

    document["version"] = "3.0.0";
    for (const auto *const mode : {"cpu", "gpu"}) {
        document["execution"] = {{"preprocessing", mode}};
        EXPECT_TRUE(opk::config::validateOpChainJson(document.dump())) << mode;
        for (const auto *const removedStage : {"inference", "postprocessing"}) {
            document["execution"][removedStage] = mode;
            EXPECT_FALSE(opk::config::validateOpChainJson(document.dump())) << removedStage;
            document["execution"].erase(removedStage);
        }
        document["ops"][0]["execution"] = mode;
        EXPECT_FALSE(opk::config::validateOpChainJson(document.dump())) << mode;
        document["ops"][0].erase("execution");
    }
    for (const auto &invalid : {nlohmann::json("auto"),
                                nlohmann::json("GPU"),
                                nlohmann::json("unknown"),
                                nlohmann::json(nullptr),
                                nlohmann::json(false)}) {
        document["execution"] = {{"preprocessing", invalid}};
        EXPECT_FALSE(opk::config::validateOpChainJson(document.dump())) << invalid;
        document.erase("execution");
        document["ops"][0]["execution"] = invalid;
        EXPECT_FALSE(opk::config::validateOpChainJson(document.dump())) << invalid;
        document["ops"][0].erase("execution");
    }
    document["execution"] = {{"unknown", "cpu"}};
    EXPECT_FALSE(opk::config::validateOpChainJson(document.dump()));
    document["execution"] = "cpu";
    EXPECT_FALSE(opk::config::validateOpChainJson(document.dump()));
    document.erase("execution");

    for (const auto *id : {"opk-future-ops/Inference", "opk-std-ops/GenericPostprocess"}) {
        document["ops"] = {{{"id", id}}};
        const auto missing = opk::config::validateOpChainJson(document.dump());
        ASSERT_FALSE(missing) << id;
        EXPECT_TRUE(hasRule(missing.error(), "schema.validation")) << id;
    }
}

TEST(ConfigValidator, OpChainAcceptsStableInstanceIds) {
    nlohmann::json document{
        {"version", "3.0.0"},
        {"name", "instance-id"},
        {"description", "Validate operation producer identities."},
        {"ops",
         {{{"id", "custom/Operation"},
           {"role", "intermediate-process"},
           {"instanceId", "python-classifier"}}}},
    };

    const auto result = opk::config::validateOpChainJson(document.dump());
    ASSERT_TRUE(result) << (result ? "" : result.error().toText());
    EXPECT_EQ(result->ops[0].instanceId, "python-classifier");

    document["ops"][0]["instanceId"] = "invalid instance";
    EXPECT_FALSE(opk::config::validateOpChainJson(document.dump()));

    document["ops"] = {
        {{"id", "custom/First"}, {"role", "intermediate-process"}, {"instanceId", "duplicate"}},
        {{"id", "custom/Second"}, {"role", "intermediate-process"}, {"instanceId", "duplicate"}},
    };
    const auto duplicate = opk::config::validateOpChainJson(document.dump());
    ASSERT_FALSE(duplicate);
    EXPECT_TRUE(hasRule(duplicate.error(), "opchain.v3.instance-id"));
}

TEST(ConfigValidator, OpChainValidatesPythonScriptAttributes) {
    nlohmann::json document{
        {"version", "3.0.0"},
        {"name", "python-script"},
        {"description", "Validate PythonScript attributes."},
        {"ops",
         {{{"id", "opk-python-ops/PythonScript"},
           {"role", "postprocess"},
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

TEST(ConfigValidator, PythonOutputDeclarationsRequireTypedCapabilities) {
    auto document = nlohmann::json::parse(R"({
        "version":"3.0.0", "name":"declared-output", "description":"Typed Python inputs",
        "ops":[{"id":"opk-python-ops/PythonScript", "role": "postprocess", "attributes":{
            "script":"script.py",
            "providedOutputs":[{"payloadType":"open_perception_kit.metadata.BoxDetections"}],
            "requiredInputs":[{"inputId":"audio", "payloadType":"open_perception_kit.metadata.VoiceActivityDetections", "producerId":"vad/output"}]
        }}]
    })");
    EXPECT_TRUE(opk::config::validateOpChainJson(document.dump()));
    document["ops"][0]["attributes"]["providedOutputs"][0]["payloadType"] = "humanFace";
    EXPECT_FALSE(opk::config::validateOpChainJson(document.dump()));
}

TEST(ConfigValidator, OpChainProjectionClearsOmittedAttributes) {
    opk::op::OpChainDescriptor::Op reused;
    reused.attributes.set("stale", true);
    nlohmann::json{{"id", "custom/Operation"}, {"role", "intermediate-process"}}.get_to(reused);
    EXPECT_TRUE(reused.attributes.raw().empty());

    opk::op::OpChainDescriptor chain;
    chain.execution = opk::op::ExecutionDefaults{.preprocessing = opk::op::ExecutionMode::Gpu};
    nlohmann::json{{"version", "3.0.0"},
                   {"name", "reused"},
                   {"description", "Clear omitted execution settings."},
                   {"ops", nlohmann::json::array()}}
        .get_to(chain);
    EXPECT_FALSE(chain.execution);
    EXPECT_THROW(nlohmann::json("unknown").get<opk::op::ExecutionMode>(), std::invalid_argument);
}

TEST(ConfigValidator, OpChainSchemaValidatesRegisteredParserContracts) {
    const auto supportedVersion = opk::config::supportedOpChainVersion();
    ASSERT_TRUE(supportedVersion) << supportedVersion.error().toText();
    EXPECT_EQ(*supportedVersion, "3.0.0");

    nlohmann::json document{
        {"version", "3.0.0"},
        {"name", "parser-contract"},
        {"description", "Validate GenericPostprocess parser attributes."},
        {"ops",
         {{{"id", "opk-std-ops/GenericPostprocess"},
           {"role", "postprocess"},
           {"attributes", {{"parser", "DummyParser"}}}}}},
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
        "RetinaFaceParser",
        "RvmParser",
        "ScrfdParser",
        "SileroVadParser",
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
        nlohmann::json{{"parser", "ObjectEmbeddingParser"}, {"embeddingSpaceId", "test:reid:v1"}},
        nlohmann::json{{"parser", "CameraContactParser"},
                       {"contactClassIndex", 0},
                       {"noContactClassIndex", 1}},
        nlohmann::json{{"parser", "GazeDetectionParser"}, {"angleBinWidthDeg", 4}},
        nlohmann::json{{"parser", "YoloParser"}, {"applyNms", false}},
        nlohmann::json{{"parser", "YoloParser"}, {"outputFormat", "cornerScoreClass"}},
        nlohmann::json{{"parser", "YoloParser"}, {"outputFormat", "cornerClassScores"}},
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
        nlohmann::json{{"parser", "ObjectEmbeddingParser"}, {"embeddingSpaceId", ""}},
        nlohmann::json{{"parser", "ObjectEmbeddingParser"}, {"embeddingSpaceId", "   "}},
        nlohmann::json{{"parser", "ObjectEmbeddingParser"}, {"embeddingSpaceId", 7}},
        nlohmann::json{{"parser", "UnknownParser"}},
        nlohmann::json{{"parser", "RetinaFaceParser"}, {"confidenceThreshold", 1.1}},
        nlohmann::json{{"parser", "RetinaFaceParser"}, {"iouThreshold", -0.1}},
        nlohmann::json{{"parser", "RetinaFaceParser"}, {"unexpected", true}},
        nlohmann::json{{"parser", "PersonClassificationParser"}, {"scoreFormat", "auto"}},
        nlohmann::json{{"parser", "GazeDetectionParser"}, {"unexpected", true}},
        nlohmann::json{{"parser", "GazeDetectionParser"}, {"angleBinWidthDeg", 0}},
        nlohmann::json{{"parser", "CameraContactParser"},
                       {"contactClassIndex", 0},
                       {"noContactClassIndex", 0}},
        nlohmann::json{{"parser", "PaddleOcrDetectionParser"}, {"gamma", 0}},
        nlohmann::json{{"parser", "YoloParser"}, {"applyNms", false}, {"iouThreshold", 0.4}},
        nlohmann::json{{"parser", "YoloParser"}, {"outputFormat", "xyxy"}},
        nlohmann::json{{"parser", "YoloParser"}, {"maxDetections", 1}},
        nlohmann::json{
            {"parser", "YoloParser"}, {"outputFormat", "centerClassScores"}, {"maxDetections", 1}},
        nlohmann::json{
            {"parser", "YoloParser"}, {"outputFormat", "cornerScoreClass"}, {"maxDetections", 1}},
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

TEST(ConfigValidator, OpChainV3RejectsControlsButAllowsUnicodeInOpId) {
    auto document = nlohmann::json{{"version", "3.0.0"},
                                   {"name", "op-id-validation"},
                                   {"description", "Validate operation identifiers."},
                                   {"ops",
                                    {{{"id", "custom/Operation"},
                                      {"role", "intermediate-process"},
                                      {"attributes", nlohmann::json::object()}}}}};

    document["ops"][0]["id"] = std::string{"custom/"} + "\xC2\x80" + "Operation";
    const auto controlResult = opk::config::validateOpChainJson(document.dump());
    ASSERT_FALSE(controlResult);
    EXPECT_TRUE(hasRule(controlResult.error(), "opchain.v3.op-id-control"));

    document["ops"][0]["id"] = std::string{"custom/"} + "\xC3\x81" + "Operation";
    const auto unicodeResult = opk::config::validateOpChainJson(document.dump());
    EXPECT_TRUE(unicodeResult) << (unicodeResult ? "" : unicodeResult.error().toText());
}

TEST(ConfigValidator, OpChainV3ValidatesLoopGroups) {
    opk::op::OpChainDescriptor descriptor;
    descriptor.ops = {
        {"custom/A", 1, {}, {}, opk::op::StageRole::IntermediateProcess},
        {"custom/B", 1, {}, {}, opk::op::StageRole::IntermediateProcess},
    };
    EXPECT_TRUE(opk::config::validateOpChainSemantics(descriptor).ok());

    descriptor.ops.pop_back();
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v3.loop-group"));

    descriptor.ops[0].loopId = 0;
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v3.loop-group"));

    descriptor.ops = {
        {"custom/A", 1, {}, {}, opk::op::StageRole::IntermediateProcess},
        {"custom/B", std::nullopt, {}, {}, opk::op::StageRole::IntermediateProcess},
        {"custom/C", 1, {}, {}, opk::op::StageRole::IntermediateProcess},
    };
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v3.loop-group"));
}

TEST(ConfigValidator, OpChainV3ValidatesBuiltInStageStructure) {
    auto descriptor = builtInStage();
    EXPECT_TRUE(opk::config::validateOpChainSemantics(descriptor).ok());

    descriptor.ops.back().id = "opk-python-ops/PythonScript";
    EXPECT_TRUE(opk::config::validateOpChainSemantics(descriptor).ok());

    descriptor = builtInStage();
    descriptor.ops.insert(descriptor.ops.end() - 1,
                          {"opk-python-ops/PythonScript",
                           std::nullopt,
                           {},
                           {},
                           opk::op::StageRole::IntermediateProcess});
    EXPECT_TRUE(opk::config::validateOpChainSemantics(descriptor).ok());

    descriptor.ops.insert(
        descriptor.ops.end() - 1,
        {"custom/Between", std::nullopt, {}, {}, opk::op::StageRole::IntermediateProcess});
    EXPECT_TRUE(opk::config::validateOpChainSemantics(descriptor).ok());

    descriptor = builtInStage();
    descriptor.ops[1].id = "custom/WrongPreprocess";
    descriptor.ops[1].role = opk::op::StageRole::IntermediateProcess;
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v3.builtin-stage"));

    descriptor = builtInStage();
    descriptor.ops[2].id = "custom/WrongInference";
    descriptor.ops[2].role = opk::op::StageRole::IntermediateProcess;
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v3.builtin-stage"));

    descriptor = builtInStage();
    descriptor.ops.back().id = "custom/WrongPostprocess";
    descriptor.ops.back().role = opk::op::StageRole::IntermediateProcess;
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v3.builtin-stage"));

    descriptor = builtInStage();
    descriptor.ops.back().id = "opk-python-ops/PythonScript";
    descriptor.ops.insert(
        descriptor.ops.end() - 1,
        {"opk-std-ops/GenericPostprocess", std::nullopt, {}, {}, opk::op::StageRole::Postprocess});
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v3.builtin-stage"));

    descriptor = builtInStage();
    descriptor.ops.insert(descriptor.ops.end() - 1,
                          {"opk-std-ops/GenericImagePreprocess",
                           std::nullopt,
                           {},
                           {},
                           opk::op::StageRole::ImagePreprocess});
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v3.builtin-stage"));

    descriptor.ops = {{"opk-std-ops/GenericImagePreprocess",
                       std::nullopt,
                       {},
                       {},
                       opk::op::StageRole::ImagePreprocess}};
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v3.builtin-stage"));
}

TEST(ConfigValidator, OpChainV3ValidatesBuiltInStageLoopOwnership) {
    auto descriptor = builtInStage();
    for (auto &op : descriptor.ops)
        op.loopId = 1;
    EXPECT_TRUE(opk::config::validateOpChainSemantics(descriptor).ok());

    descriptor.ops.insert(descriptor.ops.begin(),
                          {"custom/Before", 1, {}, {}, opk::op::StageRole::IntermediateProcess});
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v3.stage-loop"));

    descriptor = builtInStage();
    for (auto &op : descriptor.ops)
        op.loopId = 1;
    descriptor.ops.back().loopId.reset();
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v3.stage-loop"));

    descriptor = builtInStage();
    descriptor.ops.front().attributes.set("sourceProducer", "upstream/output");
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v3.stage-loop"));
}

TEST(ConfigValidator, OpChainValidatesPaddleOcrPreprocessAttributes) {
    nlohmann::json document{
        {"version", "3.0.0"},
        {"name", "paddle-ocr"},
        {"description", "Validate OCR preprocessing attributes."},
        {"ops",
         {{{"id", "opk-std-ops/InferenceController"}, {"role", "controller"}},
          {{"id", "opk-std-ops/PaddleOcrPreprocess"},
           {"role", "image-preprocess"},
           {"attributes", {{"mode", "detection"}, {"paddingValue", 0}}}},
          {{"id", "opk-onnx-ops/Inference"},
           {"role", "inference"},
           {"attributes", {{"modelDescriptor", "model.json"}}}},
          {{"id", "opk-std-ops/GenericPostprocess"},
           {"role", "postprocess"},
           {"attributes", {{"parser", "DummyParser"}}}}}},
    };
    auto &attributes = document["ops"][1]["attributes"];
    for (const auto *mode : {"detection", "recognition"}) {
        attributes = {{"mode", mode}, {"paddingValue", 0}};
        const auto result = opk::config::validateOpChainJson(document.dump());
        EXPECT_TRUE(result) << (result ? "" : result.error().toText());
        attributes["inputImageSourceName"] = "ocr-input";
        attributes["inputImageTensorIndex"] = 15;
        EXPECT_TRUE(opk::config::validateOpChainJson(document.dump()));
    }
    const auto invalid = std::array{
        nlohmann::json::object(),
        nlohmann::json{{"mode", "unknown"}, {"paddingValue", 0}},
        nlohmann::json{{"mode", 1}, {"paddingValue", 0}},
        nlohmann::json{{"mode", "detection"}, {"paddingValue", 0}, {"inputImageTensorIndex", -1}},
        nlohmann::json{{"mode", "detection"}, {"paddingValue", 0}, {"inputImageTensorIndex", 16}},
        nlohmann::json{{"mode", "detection"}, {"paddingValue", 0}, {"inputImageTensorIndex", "0"}},
        nlohmann::json{{"mode", "detection"}, {"paddingValue", 0}, {"inputImageSourceName", " "}},
        nlohmann::json{
            {"mode", "detection"}, {"paddingValue", 0}, {"inputImageSourceName", " padded"}},
        nlohmann::json{{"mode", "detection"}, {"paddingValue", 0}, {"unexpected", true}},
    };
    for (const auto &bad : invalid) {
        attributes = bad;
        const auto result = opk::config::validateOpChainJson(document.dump());
        ASSERT_FALSE(result) << bad;
        EXPECT_TRUE(hasRule(result.error(), "schema.validation")) << bad;
    }
    document["ops"][1].erase("attributes");
    EXPECT_FALSE(opk::config::validateOpChainJson(document.dump()));
}

TEST(ConfigValidator, OpChainAcceptsOcrPreprocessWithoutRelaxingStageOrderOrLoops) {
    const auto path =
        std::filesystem::path{OPK_REPOSITORY_ROOT} / "config/opchains/pp-ocrv6-tiny/opchain.json";
    std::ifstream input(path);
    ASSERT_TRUE(input);
    const auto document = nlohmann::json::parse(input);
    const auto parsed = opk::config::validateOpChainJson(document.dump(), path.string());
    ASSERT_TRUE(parsed) << parsed.error().toText();
    EXPECT_EQ(parsed->ops[1].role, opk::op::StageRole::ImagePreprocess);
    EXPECT_EQ(parsed->ops[5].role, opk::op::StageRole::ImagePreprocess);

    auto descriptor = builtInStage();
    descriptor.ops[1].id = "opk-std-ops/PaddleOcrPreprocess";
    descriptor.ops[1].role = opk::op::StageRole::IntermediateProcess;
    EXPECT_FALSE(opk::config::validateOpChainSemantics(descriptor).ok());
    descriptor.ops[1].role = opk::op::StageRole::ImagePreprocess;
    descriptor.ops[1].attributes.set("mode", "recognition");
    descriptor.ops[1].attributes.set("paddingValue", 0.0);
    descriptor.ops.front().attributes.set("sourceProducer", "upstream/output");
    for (auto &op : descriptor.ops)
        op.loopId = 1;
    EXPECT_TRUE(opk::config::validateOpChainSemantics(descriptor).ok());

    descriptor.ops[2].loopId.reset();
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v3.stage-loop"));
    descriptor.ops[2].loopId = 1;
    std::swap(descriptor.ops[1], descriptor.ops[2]);
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v3.builtin-stage"));
    std::swap(descriptor.ops[1], descriptor.ops[2]);

    for (const auto *id :
         {"opk-std-ops/GenericImagePreprocess", "opk-std-ops/PaddleOcrPreprocess"}) {
        auto duplicate = descriptor;
        auto extra = descriptor.ops[1];
        extra.id = id;
        duplicate.ops.insert(duplicate.ops.end() - 1, extra);
        EXPECT_TRUE(
            hasRule(opk::config::validateOpChainSemantics(duplicate), "opchain.v3.builtin-stage"));
    }
    descriptor.ops = {{"opk-std-ops/PaddleOcrPreprocess",
                       std::nullopt,
                       {},
                       {},
                       opk::op::StageRole::ImagePreprocess}};
    descriptor.ops[0].role = opk::op::StageRole::ImagePreprocess;
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v3.builtin-stage"));
}

TEST(ConfigValidator, OpChainV3ValidatesEffectiveThresholdOrder) {
    opk::op::OpChainDescriptor descriptor;
    descriptor.ops = {
        {"opk-std-ops/GenericPostprocess", std::nullopt, {}, {}, opk::op::StageRole::Postprocess}};
    auto &attributes = descriptor.ops.front().attributes;
    attributes.set("parser", "ModNetSegmentationParser");
    EXPECT_TRUE(opk::config::validateOpChainSemantics(descriptor).ok());

    attributes.set("thresholdLow", 0.9);
    attributes.set("thresholdHigh", 0.8);
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(descriptor), "opchain.v3.threshold-order"));

    attributes.set("parser", "PaddleOcrDetectionParser");
    attributes.set("thresholdLow", 0.6);
    attributes.set("thresholdHigh", 0.8);
    EXPECT_TRUE(opk::config::validateOpChainSemantics(descriptor).ok());
}

TEST(ConfigValidator, MobileSamUsesDefaultImageSamplingWithoutSamplingAttribute) {
    const auto path = std::filesystem::path{OPK_REPOSITORY_ROOT} /
                      "config/models/mobile-sam-int8-executorch/opchain.json";
    std::ifstream input(path);
    ASSERT_TRUE(input);
    auto document = nlohmann::json::parse(input);
    EXPECT_FALSE(document["ops"][1]["attributes"].contains("sampling"));
    ASSERT_TRUE(opk::config::validateOpChainJson(document.dump(), path.string()));
    for (const auto *const sampling : {"nearest", "bilinear"}) {
        document["ops"][1]["attributes"]["sampling"] = sampling;
        EXPECT_TRUE(opk::config::validateOpChainJson(document.dump(), path.string()));
    }
    document["ops"][1]["attributes"]["sampling"] = "unknown";
    EXPECT_FALSE(opk::config::validateOpChainJson(document.dump(), path.string()));
}

TEST(ConfigValidator, ExplicitImageBindingDefersCustomInputCapabilitiesToRuntime) {
    const auto path = std::filesystem::path{OPK_REPOSITORY_ROOT} /
                      "config/models/mobile-sam-int8-executorch/opchain.json";
    std::ifstream input(path);
    ASSERT_TRUE(input);
    auto document = nlohmann::json::parse(input);
    document["ops"][2]["id"] = "custom/InputPreprocessor";
    const auto contents = document.dump();
    const auto sourcePath = path.string();
    const auto validation = opk::config::validateOpChainJson(contents, sourcePath);
    EXPECT_TRUE(validation);

    auto descriptor = builtInStage();
    descriptor.ops[1].attributes.set("inferenceInstance", "sam");
    descriptor.ops[2].instanceId = "sam";
    descriptor.ops.insert(
        descriptor.ops.begin() + 2,
        {"custom/InputPreprocessor", std::nullopt, {}, {}, opk::op::StageRole::InputPreprocess});
    EXPECT_TRUE(opk::config::validateOpChainSemantics(descriptor).ok());
    descriptor.ops.insert(descriptor.ops.begin() + 2,
                          {"custom/AnotherInputPreprocessor",
                           std::nullopt,
                           {},
                           {},
                           opk::op::StageRole::InputPreprocess});
    EXPECT_TRUE(opk::config::validateOpChainSemantics(descriptor).ok());
    auto invalid = descriptor;
    invalid.ops[1].attributes = opk::AttributeMap{};
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(invalid), "opchain.v3.builtin-stage"));
    invalid = descriptor;
    invalid.ops[1].attributes.set("inferenceInstance", "other");
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(invalid), "opchain.v3.builtin-stage"));
    invalid = descriptor;
    invalid.ops[2].loopId = 1;
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(invalid), "opchain.v3.builtin-stage"));
    invalid = descriptor;
    invalid.ops[2].id = "opk-std-ops/GenericImagePreprocess";
    invalid.ops[2].role = opk::op::StageRole::ImagePreprocess;
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(invalid), "opchain.v3.builtin-stage"));
}

TEST(ConfigValidator, BoxSpecificCrossOperationChecksAreDeferredToBinding) {
    auto descriptor = builtInStage();
    descriptor.ops[1].attributes.set("inferenceInstance", "sam");
    descriptor.ops[2].instanceId = "sam";
    opk::op::OpChainDescriptor::Op box{"opk-std-ops/BoxPromptPreprocess",
                                       std::nullopt,
                                       {},
                                       {},
                                       opk::op::StageRole::InputPreprocess};
    box.attributes.set("inferenceInstance", "sam");
    box.attributes.set("inputBoxTensorIndex", int64_t{1});
    descriptor.ops.insert(descriptor.ops.begin() + 2, box);
    EXPECT_TRUE(opk::config::validateOpChainSemantics(descriptor).ok());

    auto invalid = descriptor;
    invalid.ops[1].attributes.set("inferenceInstance", "other");
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(invalid), "opchain.v3.builtin-stage"));
    invalid = descriptor;
    invalid.ops[2].attributes.set("inputBoxTensorIndex", int64_t{0});
    EXPECT_TRUE(opk::config::validateOpChainSemantics(invalid).ok());
    invalid = descriptor;
    invalid.ops[2].attributes.set("inputImageSourceName", "other");
    EXPECT_TRUE(opk::config::validateOpChainSemantics(invalid).ok());
    invalid = descriptor;
    invalid.ops[2].attributes.set("inferenceInstance", "other");
    EXPECT_TRUE(opk::config::validateOpChainSemantics(invalid).ok());
    invalid = descriptor;
    std::swap(invalid.ops[2], invalid.ops[3]);
    EXPECT_FALSE(opk::config::validateOpChainSemantics(invalid).ok());
    invalid = descriptor;
    for (auto &op : invalid.ops) {
        op.loopId = 1;
    }
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(invalid), "opchain.v3.builtin-stage"));
}

TEST(ConfigValidator, MobileSamDescriptorAndPromptAttributesUsePublishedFormat) {
    const auto path = std::filesystem::path{OPK_REPOSITORY_ROOT} /
                      "config/models/mobile-sam-int8-executorch/opchain.json";
    std::ifstream file(path);
    ASSERT_TRUE(file);
    const auto original = nlohmann::json::parse(file);
    const auto valid = opk::config::validateOpChainJson(original.dump());
    ASSERT_TRUE(valid) << valid.error().toText();
    EXPECT_TRUE(opk::config::validateOpChainSemantics(*valid).ok());
    for (const auto &attributes :
         {nlohmann::json{{"inferenceInstance", "sam"}},
          nlohmann::json{{"inferenceInstance", "sam"}, {"inputBoxTensorIndex", -1}},
          nlohmann::json{{"inferenceInstance", "sam"}, {"inputBoxTensorIndex", 16}},
          nlohmann::json{
              {"inferenceInstance", "sam"}, {"inputBoxTensorIndex", 1}, {"unknown", 0}}}) {
        auto invalid = original;
        invalid["ops"][2]["attributes"] = attributes;
        EXPECT_FALSE(opk::config::validateOpChainJson(invalid.dump()));
    }
}

} // namespace

TEST(ConfigValidator, UltraFaceLogitsUseOpChain2Contract) {
    nlohmann::json document{
        {"version", "3.0.0"},
        {"name", "ultraface-logit-schema"},
        {"description", "Validate the logits attribute without a runtime artifact."},
        {"ops",
         {{{"id", "opk-std-ops/GenericPostprocess"},
           {"role", "postprocess"},
           {"attributes", {{"parser", "UltrafaceParser"}, {"scoresAreLogits", true}}}}}},
    };
    const auto validated = opk::config::validateOpChainJson(document.dump(), "opchain.json");
    ASSERT_TRUE(validated) << validated.error().toText();

    document["ops"][0]["attributes"]["scoresAreLogits"] = "true";
    EXPECT_FALSE(opk::config::validateOpChainJson(document.dump(), "opchain.json"));
}

TEST(ConfigValidator, QueryStageKeepsExplicitImageBindingAndTerminalContract) {
    auto descriptor = builtInStage();
    descriptor.ops[1].id = "custom/Image";
    descriptor.ops[1].role = opk::op::StageRole::ImagePreprocess;
    descriptor.ops[1].attributes.set("inferenceInstance", "query");
    descriptor.ops[2].instanceId = "query";
    descriptor.ops[3].id = "custom/Results";
    descriptor.ops[3].role = opk::op::StageRole::Postprocess;
    descriptor.ops.insert(descriptor.ops.begin() + 2,
                          {"opk-python-ops/YoloePromptPack",
                           std::nullopt,
                           {},
                           {},
                           opk::op::StageRole::InputPreprocess});
    descriptor.ops.insert(descriptor.ops.begin(),
                          {"opk-python-ops/YoloePromptPrepare",
                           std::nullopt,
                           {},
                           {},
                           opk::op::StageRole::IntermediateProcess});
    EXPECT_TRUE(opk::config::validateOpChainSemantics(descriptor).ok());

    auto invalid = descriptor;
    invalid.ops[2].attributes = opk::AttributeMap{};
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(invalid), "opchain.v3.builtin-stage"));
    invalid = descriptor;
    invalid.ops[2].attributes.set("inferenceInstance", "other");
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(invalid), "opchain.v3.builtin-stage"));
    invalid = descriptor;
    invalid.ops.back().id = "custom/NotTerminal";
    invalid.ops.back().role = opk::op::StageRole::IntermediateProcess;
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(invalid), "opchain.v3.builtin-stage"));
    invalid = descriptor;
    invalid.ops.insert(invalid.ops.end() - 1, invalid.ops.back());
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(invalid), "opchain.v3.builtin-stage"));
    invalid = descriptor;
    invalid.ops[3].role = opk::op::StageRole::ImagePreprocess;
    EXPECT_TRUE(
        hasRule(opk::config::validateOpChainSemantics(invalid), "opchain.v3.builtin-stage"));
}

TEST(ConfigValidator, ExplicitIntermediateRoleDoesNotReplaceStageContracts) {
    using opk::op::StageRole;
    auto descriptor = builtInStage();
    descriptor.ops.back().id = "opk-python-ops/PythonScript";
    descriptor.ops.back().role = StageRole::Postprocess;
    auto intermediate = descriptor.ops.back();
    intermediate.role = StageRole::IntermediateProcess;
    descriptor.ops.insert(descriptor.ops.end() - 1, intermediate);
    EXPECT_TRUE(opk::config::validateOpChainSemantics(descriptor).ok());
    descriptor.ops.back().role = StageRole::IntermediateProcess;
    EXPECT_FALSE(opk::config::validateOpChainSemantics(descriptor).ok());
    descriptor = builtInStage();
    descriptor.ops[1].role = StageRole::IntermediateProcess;
    EXPECT_FALSE(opk::config::validateOpChainSemantics(descriptor).ok());
    descriptor = builtInStage();
    descriptor.ops.insert(descriptor.ops.begin() + 2, intermediate);
    descriptor.ops[1].attributes.set("inferenceInstance", "infer");
    descriptor.ops[3].instanceId = "infer";
    EXPECT_FALSE(opk::config::validateOpChainSemantics(descriptor).ok());
}

TEST(ConfigValidator, ExplicitRolesClassifyCustomStageNames) {
    auto descriptor = builtInStage();
    for (size_t index = 0; index < descriptor.ops.size(); ++index) {
        descriptor.ops[index].id = "custom/Implementation" + std::to_string(index);
    }
    EXPECT_TRUE(opk::config::validateOpChainSemantics(descriptor).ok());
}
