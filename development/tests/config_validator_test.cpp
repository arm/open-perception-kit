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
#include "opk/AttributeMap.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

using opk::config::test::hasRule;

TEST(ConfigValidator, UsesSchemaVersionsForDiagnostics) {
    using namespace opk::config;
    const auto modelVersion = supportedModelVersion();
    const auto opchainVersion = supportedOpChainVersion();
    const auto pipelineVersion = supportedPipelineVersion();
    ASSERT_TRUE(modelVersion) << modelVersion.error().toText();
    ASSERT_TRUE(opchainVersion) << opchainVersion.error().toText();
    ASSERT_TRUE(pipelineVersion) << pipelineVersion.error().toText();

    const auto modelResult = validateModelJson("{}");
    const auto opchainResult = validateOpChainJson("{}");
    const auto pipelineResult = validatePipelineJson("{}");
    ASSERT_FALSE(modelResult);
    ASSERT_FALSE(opchainResult);
    ASSERT_FALSE(pipelineResult);

    EXPECT_NE(modelResult.error().toText().find("supported version: " + *modelVersion),
              std::string::npos);
    EXPECT_NE(opchainResult.error().toText().find("supported version: " + *opchainVersion),
              std::string::npos);
    EXPECT_NE(pipelineResult.error().toText().find("supported version: " + *pipelineVersion),
              std::string::npos);
}

TEST(AttributeMap, NumericGettersAndMissingDefaultsRemainStrict) {
    opk::AttributeMap attributes;
    attributes.set("integer", std::int64_t{3}).set("text", "wrong type");

    EXPECT_FLOAT_EQ(attributes.getFloat("integer"), 3.0F);
    EXPECT_DOUBLE_EQ(attributes.getDouble("integer"), 3.0);
    EXPECT_FLOAT_EQ(attributes.getFloatOrDefault("missing", 1.5F), 1.5F);
    EXPECT_THROW(attributes.getFloatOrDefault("text", 1.5F), std::bad_variant_access);
}

TEST(ConfigValidator, RejectsNestedDuplicateBeforeSchema) {
    const auto result = opk::config::validateModelJson(
        R"({"version":"1.0.0","nested":{"x":1,"x":2}})", "duplicate.json");

    ASSERT_FALSE(result);
    EXPECT_TRUE(hasRule(result.error(), "json.duplicate-key"));
    EXPECT_EQ(result.error().issues.front().phase, opk::config::ValidationPhase::Parse);
    EXPECT_TRUE(result.error().issues.front().instanceLocation.empty());
}

TEST(ConfigValidator, RejectsInvalidUtf8BeforeSchema) {
    std::string document = R"({"version":"1.0.0","name":")";
    document.push_back(static_cast<char>(0xff));
    document += R"("})";

    const auto result = opk::config::validateModelJson(document, "invalid-utf8.json");

    ASSERT_FALSE(result);
    EXPECT_TRUE(hasRule(result.error(), "json.syntax"));
    EXPECT_EQ(result.error().issues.front().phase, opk::config::ValidationPhase::Parse);
}

TEST(ConfigValidator, RejectsUnsupportedVersionDuringDispatch) {
    const auto result = opk::config::validateModelJson(R"({"version":"2.0.0"})", "model.json");

    ASSERT_FALSE(result);
    EXPECT_TRUE(hasRule(result.error(), "dispatch.unsupported"));
    EXPECT_EQ(result.error().issues.front().instanceLocation, "/version");
}

TEST(ConfigValidator, RequiresVersion) {
    const auto result = opk::config::validateModelJson(R"({})", "model.json");

    ASSERT_FALSE(result);
}

TEST(ConfigValidator, AllConfigurationContractsRejectInvalidVersions) {
    for (const auto *document : {R"({})",
                                 R"({"version":null})",
                                 R"({"version":true})",
                                 R"({"version":1})",
                                 R"({"version":"1"})",
                                 R"({"version":"1.0"})",
                                 R"({"version":"1.0.0.0"})",
                                 R"({"version":"01.0.0"})",
                                 R"({"version":"1.01.0"})",
                                 R"({"version":"1.0.01"})",
                                 R"({"version":"1.0.0-rc1"})",
                                 R"({"version":"1.0.0+build"})",
                                 R"({"version":"1.0.0\n"})",
                                 R"({"version":"1.0.0\u0000"})",
                                 R"({"version":"0.9.9"})",
                                 R"({"version":"2.0.0"})",
                                 R"({"version":1.0})",
                                 R"({"version":0})",
                                 R"({"version":-1})",
                                 R"({"version":"2.0.0"})",
                                 R"({"version":18446744073709551615})"}) {
        SCOPED_TRACE(document);
        const auto model = opk::config::validateModelJson(document);
        const auto opchain = opk::config::validateOpChainJson(document);
        const auto pipeline = opk::config::validatePipelineJson(document, "/custom/demo.json");
        ASSERT_FALSE(model);
        ASSERT_FALSE(opchain);
        ASSERT_FALSE(pipeline);
        for (const auto *report : {&model.error(), &opchain.error(), &pipeline.error()}) {
            EXPECT_FALSE(report->ok());
        }
    }
}

TEST(ConfigValidator, PipelineV1ValidatesExecutableFieldsAndPreservesAnnotations) {
    nlohmann::json document{{"version", "1.0.0"},
                            {"description", "Pipeline test"},
                            {"pipeline", "fakesrc ! fakesink"},
                            {"loop", true},
                            {"alternative-source-image", {"filesrc", "! jpegdec"}}};
    const auto valid = opk::config::validatePipelineJson(document.dump());
    ASSERT_TRUE(valid) << valid.error().toText();
    EXPECT_EQ(*valid, document);

    document["pipeline"] = {"", "fakesrc", "! fakesink"};
    EXPECT_TRUE(opk::config::validatePipelineJson(document.dump()));
    for (const auto &invalid : {nlohmann::json(""),
                                nlohmann::json(" \t\n"),
                                nlohmann::json::array(),
                                nlohmann::json({"", " "}),
                                nlohmann::json({"fakesrc", 42}),
                                nlohmann::json(42)}) {
        document["pipeline"] = invalid;
        const auto result = opk::config::validatePipelineJson(document.dump());
        ASSERT_FALSE(result) << document;
        EXPECT_TRUE(hasRule(result.error(), "schema.validation"));
    }
    const std::string nulCommand("fakesrc\0ignored", 15);
    for (const auto &invalid : {nlohmann::json(nulCommand), nlohmann::json({nulCommand})}) {
        document["pipeline"] = invalid;
        const auto result = opk::config::validatePipelineJson(document.dump());
        ASSERT_FALSE(result);
        EXPECT_TRUE(hasRule(result.error(), "pipeline.v1.command"));
    }
    document["pipeline"] = "fakesrc ! fakesink";
    document["loop"] = "true";
    EXPECT_FALSE(opk::config::validatePipelineJson(document.dump()));
    EXPECT_FALSE(opk::config::validatePipelineJson(
        R"({"version":"2.0.0","version":"1.0.0","description":"duplicate","pipeline":"fakesrc"})"));
}

TEST(ConfigValidator, CompatibleMinorAndPatchVersionsSurviveRoundTrips) {
    for (const auto *version : {"1.0.0", "1.0.42", "1.7.13"}) {
        SCOPED_TRACE(version);
        const nlohmann::json model{
            {"version", version},
            {"name", "Model"},
            {"modelFile", "model.onnx"},
            {"dynamicOutput", true},
            {"inputTensors",
             {{{"shape", {1}}, {"dataKind", "RawTensorData"}, {"valueType", "Float32"}}}}};
        const nlohmann::json opchain{
            {"version", version},
            {"name", "Chain"},
            {"description", "Version test"},
            {"ops", {{{"id", "custom/Operation"}, {"attributes", nlohmann::json::object()}}}}};
        const nlohmann::json pipeline{{"version", version},
                                      {"description", "Version test"},
                                      {"pipeline", "fakesrc ! fakesink"}};
        const auto modelResult = opk::config::validateModelJson(model.dump());
        const auto chainResult = opk::config::validateOpChainJson(opchain.dump());
        const auto pipelineResult = opk::config::validatePipelineJson(pipeline.dump());
        ASSERT_TRUE(modelResult) << modelResult.error().toText();
        ASSERT_TRUE(chainResult) << chainResult.error().toText();
        ASSERT_TRUE(pipelineResult) << pipelineResult.error().toText();
        EXPECT_EQ(nlohmann::json(*modelResult).at("version"), version);
        EXPECT_EQ(nlohmann::json(*chainResult).at("version"), version);
        EXPECT_EQ(pipelineResult->at("version"), version);
    }
}

TEST(ConfigValidator, AcceptsNonEmptyModelVariantFilename) {
    const auto result = opk::config::validateModelJson(R"({
        "version": "1.0.0",
        "name": "variant",
        "modelFile": "model.onnx",
        "dynamicOutput": true,
        "inputTensors": [
            {"shape": [1], "dataKind": "RawTensorData", "valueType": "Float32"}
        ]
    })",
                                                       "model-recognition.json");

    EXPECT_TRUE(result) << result.error().toText();
}

TEST(ConfigValidator, ModelSchemaSeparatesBuildDownloadFromLocalRuntimePath) {
    nlohmann::json document{
        {"version", "1.0.0"},
        {"name", "downloaded-model"},
        {"modelFile", "models/model.onnx"},
        {"hfDownload",
         {{"repo_id", "Arm/example"},
          {"revision", "0123456789abcdef0123456789abcdef01234567"}, // pragma: allowlist secret
          {"filename", "onnx/model.onnx"}}},
        {"dynamicOutput", true},
        {"inputTensors",
         {{{"shape", {1}}, {"dataKind", "RawTensorData"}, {"valueType", "Float32"}}}}};
    const auto download = document["hfDownload"];

    const auto valid = opk::config::validateModelJson(document.dump());

    ASSERT_TRUE(valid) << valid.error().toText();
    EXPECT_FALSE(nlohmann::json(*valid).contains("hfDownload"));

    for (const auto *modelFile : {"../other/model.onnx", "/opt/models/external.onnx"}) {
        document["modelFile"] = modelFile;
        const auto escapedDownload = opk::config::validateModelJson(document.dump());
        ASSERT_FALSE(escapedDownload);
        EXPECT_TRUE(hasRule(escapedDownload.error(), "schema.validation"));

        document.erase("hfDownload");
        const auto externalRuntimeModel = opk::config::validateModelJson(document.dump());
        ASSERT_TRUE(externalRuntimeModel) << externalRuntimeModel.error().toText();
        document["hfDownload"] = download;
    }

    document["modelFile"] =
        "hf:Arm/example@0123456789abcdef0123456789abcdef01234567#file=model.onnx";
    const auto legacyLocator = opk::config::validateModelJson(document.dump());
    ASSERT_FALSE(legacyLocator);
    EXPECT_TRUE(hasRule(legacyLocator.error(), "schema.validation"));

    document["modelFile"] = "model.onnx";
    document["hfDownload"].erase("filename");
    const auto incompleteDownload = opk::config::validateModelJson(document.dump());
    ASSERT_FALSE(incompleteDownload);
    EXPECT_TRUE(hasRule(incompleteDownload.error(), "schema.validation"));
}

TEST(ConfigValidator, TypedValidationRejectsMismatchedRoutingFilename) {
    const auto model = opk::config::validateModelJson(R"({
        "version": "1.0.0",
        "name": "model",
        "modelFile": "model.onnx",
        "dynamicOutput": true,
        "inputTensors": [
            {"shape": [1], "dataKind": "RawTensorData", "valueType": "Float32"}
        ]
    })",
                                                      "opchain.json");
    const auto opchain = opk::config::validateOpChainJson(R"({
        "version": "1.0.0",
        "name": "opchain",
        "description": "Routing test.",
        "ops": [{"id": "custom/Operation", "attributes": {}}]
    })",
                                                          "model.json");

    ASSERT_FALSE(model);
    ASSERT_FALSE(opchain);
    EXPECT_TRUE(hasRule(model.error(), "dispatch.filename"));
    EXPECT_TRUE(hasRule(opchain.error(), "dispatch.filename"));
}

TEST(ConfigValidator, SchemasRejectValuesThatCannotProjectToFiniteFloat) {
    const auto valueInput = opk::config::validateModelJson(R"({
        "version": "1.0.0",
        "name": "large-value",
        "modelFile": "model.onnx",
        "dynamicOutput": true,
        "inputTensors": [{"dataKind": "Value", "valueInputs": [1e308]}]
    })");
    const auto imageMean = opk::config::validateModelJson(R"({
        "version": "1.0.0",
        "name": "large-mean",
        "modelFile": "model.onnx",
        "dynamicOutput": true,
        "inputTensors": [{
            "shape": [1, 3, 1, 1],
            "dataKind": "ImageRgbChw",
            "mean": [1e308, 0, 0]
        }]
    })");
    const auto gamma = opk::config::validateOpChainJson(R"({
        "version": "1.0.0",
        "name": "large-gamma",
        "description": "Reject gamma that narrows to infinity.",
        "ops": [{
            "id": "opk-std-ops/GenericPostprocess",
            "attributes": {"parser": "PaddleOcrDetectionParser", "gamma": 1e308}
        }]
    })");

    ASSERT_FALSE(valueInput);
    ASSERT_FALSE(imageMean);
    ASSERT_FALSE(gamma);
    for (const auto *report : {&valueInput.error(), &imageMean.error(), &gamma.error()})
        EXPECT_TRUE(hasRule(*report, "schema.validation"));
}

TEST(ConfigValidator, ModelAndOpChainSemanticsRejectControlCharactersInNames) {
    nlohmann::json model{
        {"version", "1.0.0"},
        {"name", "model"},
        {"modelFile", "model.onnx"},
        {"dynamicOutput", true},
        {"inputTensors",
         {{{"shape", {1}}, {"dataKind", "RawTensorData"}, {"valueType", "Float32"}}}}};
    nlohmann::json opchain{
        {"version", "1.0.0"},
        {"name", "opchain"},
        {"description", "Control-character test."},
        {"ops", {{{"id", "custom/Operation"}, {"attributes", nlohmann::json::object()}}}}};

    for (const std::string &control :
         {std::string{"\0", 1}, std::string{"\x7f", 1}, std::string{"\xc2\x80", 2}}) {
        model["name"] = "model" + control + "name";
        opchain["name"] = "opchain" + control + "name";

        const auto modelResult = opk::config::validateModelJson(model.dump());
        const auto opchainResult = opk::config::validateOpChainJson(opchain.dump());

        ASSERT_FALSE(modelResult);
        ASSERT_FALSE(opchainResult);
        EXPECT_TRUE(hasRule(modelResult.error(), "common.v1.name-control"));
        EXPECT_TRUE(hasRule(opchainResult.error(), "common.v1.name-control"));
    }
}

TEST(ConfigValidator, CanonicalModelAndOpChainSerializationRevalidates) {
    const auto model = opk::config::validateModelJson(R"({
        "version": "1.0.0",
        "name": "roundtrip-model",
        "modelFile": "model.onnx",
        "dynamicOutput": true,
        "inputTensors": [
            {"shape": [1], "dataKind": "RawTensorData", "valueType": "Float32"}
        ]
    })");
    const auto opchain = opk::config::validateOpChainJson(R"({
        "version": "1.0.0",
        "name": "roundtrip-opchain",
        "description": "Round-trip a custom operation.",
        "displayName": "Round-trip model",
        "task": "Contract validation",
        "runtime": "CustomRT",
        "ops": [{"id": "custom/Operation", "attributes": {"nested": [1, true, null]}}]
    })");

    ASSERT_TRUE(model) << model.error().toText();
    ASSERT_TRUE(opchain) << opchain.error().toText();
    ASSERT_EQ(opchain->ops.size(), 1U);
    EXPECT_FALSE(opchain->ops[0].loopId.has_value());
    EXPECT_EQ(opchain->displayName, "Round-trip model");
    EXPECT_EQ(opchain->task, "Contract validation");
    EXPECT_EQ(opchain->runtime, "CustomRT");

    const auto revalidatedModel = opk::config::validateModelJson(nlohmann::json(*model).dump());
    const nlohmann::json serializedOpChain = *opchain;
    EXPECT_FALSE(serializedOpChain["ops"][0].contains("loopId"));
    const auto revalidatedOpChain = opk::config::validateOpChainJson(serializedOpChain.dump());

    EXPECT_TRUE(revalidatedModel) << (revalidatedModel ? "" : revalidatedModel.error().toText());
    EXPECT_TRUE(revalidatedOpChain)
        << (revalidatedOpChain ? "" : revalidatedOpChain.error().toText());
}

TEST(ConfigValidator, OpChainDisplayMetadataUsesNonEmptyStrings) {
    nlohmann::json opchain{
        {"version", "1.0.0"},
        {"name", "display-metadata"},
        {"description", "Validate optional display metadata."},
        {"displayName", "Display name"},
        {"task", "Object detection"},
        {"runtime", "ONNX"},
        {"ops", {{{"id", "custom/Operation"}, {"attributes", nlohmann::json::object()}}}}};

    EXPECT_TRUE(opk::config::validateOpChainJson(opchain.dump()));

    for (const auto *field : {"displayName", "task", "runtime"}) {
        opchain[field] = "";
        const auto result = opk::config::validateOpChainJson(opchain.dump());
        ASSERT_FALSE(result);
        EXPECT_TRUE(hasRule(result.error(), "schema.validation"));
        opchain[field] = "valid";
    }
}

TEST(ConfigValidator, SemanticsRejectEmbeddedNullAtRuntimeStringBoundaries) {
    const std::string embeddedNull{"unsafe\0value", 12};
    nlohmann::json model{
        {"version", "1.0.0"},
        {"name", "model"},
        {"modelFile", embeddedNull},
        {"dynamicOutput", true},
        {"inputTensors",
         {{{"shape", {1}}, {"dataKind", "RawTensorData"}, {"valueType", "Float32"}}}}};
    nlohmann::json opchain{
        {"version", "1.0.0"},
        {"name", "opchain"},
        {"description", "Runtime boundary controls."},
        {"ops", {{{"id", "custom/Operation"}, {"attributes", nlohmann::json::object()}}}}};

    const auto modelResult = opk::config::validateModelJson(model.dump());
    opchain["ops"][0]["id"] = "custom/" + embeddedNull;
    const auto opIdResult = opk::config::validateOpChainJson(opchain.dump());
    opchain["ops"][0] = {{"id", "opk-future-ops/Inference"},
                         {"attributes", {{"modelDescriptor", embeddedNull}}}};
    const auto modelDescriptorResult = opk::config::validateOpChainJson(opchain.dump());

    ASSERT_FALSE(modelResult);
    ASSERT_FALSE(opIdResult);
    ASSERT_FALSE(modelDescriptorResult);
    EXPECT_TRUE(hasRule(modelResult.error(), "model.v1.model-file-control"));
    EXPECT_TRUE(hasRule(opIdResult.error(), "opchain.v1.op-id-control"));
    EXPECT_TRUE(hasRule(modelDescriptorResult.error(), "opchain.v1.model-descriptor-control"));
}

} // namespace
