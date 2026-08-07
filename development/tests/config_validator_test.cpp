/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "config_validator_test_support.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

using pek::config::test::hasRule;

TEST(ConfigValidator, RejectsNestedDuplicateBeforeSchema) {
    const auto result =
        pek::config::validateModelJson(R"({"version":1,"nested":{"x":1,"x":2}})", "duplicate.json");

    ASSERT_FALSE(result);
    EXPECT_TRUE(hasRule(result.error(), "json.duplicate-key"));
    EXPECT_EQ(result.error().issues.front().phase, pek::config::ValidationPhase::Parse);
    EXPECT_TRUE(result.error().issues.front().instanceLocation.empty());
}

TEST(ConfigValidator, RejectsInvalidUtf8BeforeSchema) {
    std::string document = R"({"version":1,"name":")";
    document.push_back(static_cast<char>(0xff));
    document += R"("})";

    const auto result = pek::config::validateModelJson(document, "invalid-utf8.json");

    ASSERT_FALSE(result);
    EXPECT_TRUE(hasRule(result.error(), "json.syntax"));
    EXPECT_EQ(result.error().issues.front().phase, pek::config::ValidationPhase::Parse);
}

TEST(ConfigValidator, RejectsUnsupportedVersionDuringDispatch) {
    const auto result = pek::config::validateModelJson(R"({"version":2})", "model.json");

    ASSERT_FALSE(result);
    EXPECT_TRUE(hasRule(result.error(), "dispatch.unsupported"));
    EXPECT_EQ(result.error().issues.front().instanceLocation, "/version");
}

TEST(ConfigValidator, RequiresVersionDuringDispatch) {
    const auto result = pek::config::validateModelJson(R"({})", "model.json");

    ASSERT_FALSE(result);
    EXPECT_TRUE(hasRule(result.error(), "dispatch.version"));
    EXPECT_EQ(result.error().issues.front().instanceLocation, "/version");
}

TEST(ConfigValidator, AcceptsNonEmptyModelVariantFilename) {
    const auto result = pek::config::validateModelJson(R"({
        "version": 1,
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
        {"version", 1},
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

    const auto valid = pek::config::validateModelJson(document.dump());

    ASSERT_TRUE(valid) << valid.error().toText();
    EXPECT_FALSE(nlohmann::json(valid->value()).contains("hfDownload"));

    for (const auto *modelFile : {"../other/model.onnx", "/opt/models/external.onnx"}) {
        document["modelFile"] = modelFile;
        const auto escapedDownload = pek::config::validateModelJson(document.dump());
        ASSERT_FALSE(escapedDownload);
        EXPECT_TRUE(hasRule(escapedDownload.error(), "schema.validation"));

        document.erase("hfDownload");
        const auto externalRuntimeModel = pek::config::validateModelJson(document.dump());
        ASSERT_TRUE(externalRuntimeModel) << externalRuntimeModel.error().toText();
        document["hfDownload"] = download;
    }

    document["modelFile"] =
        "hf:Arm/example@0123456789abcdef0123456789abcdef01234567#file=model.onnx";
    const auto legacyLocator = pek::config::validateModelJson(document.dump());
    ASSERT_FALSE(legacyLocator);
    EXPECT_TRUE(hasRule(legacyLocator.error(), "schema.validation"));

    document["modelFile"] = "model.onnx";
    document["hfDownload"].erase("filename");
    const auto incompleteDownload = pek::config::validateModelJson(document.dump());
    ASSERT_FALSE(incompleteDownload);
    EXPECT_TRUE(hasRule(incompleteDownload.error(), "schema.validation"));
}

TEST(ConfigValidator, TypedValidationRejectsMismatchedRoutingFilename) {
    const auto model = pek::config::validateModelJson(R"({
        "version": 1,
        "name": "model",
        "modelFile": "model.onnx",
        "dynamicOutput": true,
        "inputTensors": [
            {"shape": [1], "dataKind": "RawTensorData", "valueType": "Float32"}
        ]
    })",
                                                      "opchain.json");
    const auto opchain = pek::config::validateOpChainJson(R"({
        "version": 1,
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
    const auto valueInput = pek::config::validateModelJson(R"({
        "version": 1,
        "name": "large-value",
        "modelFile": "model.onnx",
        "dynamicOutput": true,
        "inputTensors": [{"dataKind": "Value", "valueInputs": [1e308]}]
    })");
    const auto imageMean = pek::config::validateModelJson(R"({
        "version": 1,
        "name": "large-mean",
        "modelFile": "model.onnx",
        "dynamicOutput": true,
        "inputTensors": [{
            "shape": [1, 3, 1, 1],
            "dataKind": "ImageRgbChw",
            "mean": [1e308, 0, 0]
        }]
    })");
    const auto gamma = pek::config::validateOpChainJson(R"({
        "version": 1,
        "name": "large-gamma",
        "description": "Reject gamma that narrows to infinity.",
        "ops": [{
            "id": "pek-std-ops/GenericPostprocess",
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
        {"version", 1},
        {"name", "model"},
        {"modelFile", "model.onnx"},
        {"dynamicOutput", true},
        {"inputTensors",
         {{{"shape", {1}}, {"dataKind", "RawTensorData"}, {"valueType", "Float32"}}}}};
    nlohmann::json opchain{
        {"version", 1},
        {"name", "opchain"},
        {"description", "Control-character test."},
        {"ops", {{{"id", "custom/Operation"}, {"attributes", nlohmann::json::object()}}}}};

    for (const std::string &control :
         {std::string{"\0", 1}, std::string{"\x7f", 1}, std::string{"\xc2\x80", 2}}) {
        model["name"] = "model" + control + "name";
        opchain["name"] = "opchain" + control + "name";

        const auto modelResult = pek::config::validateModelJson(model.dump());
        const auto opchainResult = pek::config::validateOpChainJson(opchain.dump());

        ASSERT_FALSE(modelResult);
        ASSERT_FALSE(opchainResult);
        EXPECT_TRUE(hasRule(modelResult.error(), "common.v1.name-control"));
        EXPECT_TRUE(hasRule(opchainResult.error(), "common.v1.name-control"));
    }
}

TEST(ConfigValidator, CanonicalModelAndOpChainSerializationRevalidates) {
    const auto model = pek::config::validateModelJson(R"({
        "version": 1,
        "name": "roundtrip-model",
        "modelFile": "model.onnx",
        "dynamicOutput": true,
        "inputTensors": [
            {"shape": [1], "dataKind": "RawTensorData", "valueType": "Float32"}
        ]
    })");
    const auto opchain = pek::config::validateOpChainJson(R"({
        "version": 1,
        "name": "roundtrip-opchain",
        "description": "Round-trip a custom operation.",
        "ops": [{"id": "custom/Operation", "attributes": {"nested": [1, true, null]}}]
    })");

    ASSERT_TRUE(model) << model.error().toText();
    ASSERT_TRUE(opchain) << opchain.error().toText();
    ASSERT_EQ(opchain->value().ops.size(), 1U);
    EXPECT_FALSE(opchain->value().ops[0].loopId.has_value());

    const auto revalidatedModel =
        pek::config::validateModelJson(nlohmann::json(model->value()).dump());
    const nlohmann::json serializedOpChain = opchain->value();
    EXPECT_FALSE(serializedOpChain["ops"][0].contains("loopId"));
    const auto revalidatedOpChain = pek::config::validateOpChainJson(serializedOpChain.dump());

    EXPECT_TRUE(revalidatedModel) << (revalidatedModel ? "" : revalidatedModel.error().toText());
    EXPECT_TRUE(revalidatedOpChain)
        << (revalidatedOpChain ? "" : revalidatedOpChain.error().toText());
}

TEST(ConfigValidator, SemanticsRejectEmbeddedNullAtRuntimeStringBoundaries) {
    const std::string embeddedNull{"unsafe\0value", 12};
    nlohmann::json model{
        {"version", 1},
        {"name", "model"},
        {"modelFile", embeddedNull},
        {"dynamicOutput", true},
        {"inputTensors",
         {{{"shape", {1}}, {"dataKind", "RawTensorData"}, {"valueType", "Float32"}}}}};
    nlohmann::json opchain{
        {"version", 1},
        {"name", "opchain"},
        {"description", "Runtime boundary controls."},
        {"ops", {{{"id", "custom/Operation"}, {"attributes", nlohmann::json::object()}}}}};

    const auto modelResult = pek::config::validateModelJson(model.dump());
    opchain["ops"][0]["id"] = "custom/" + embeddedNull;
    const auto opIdResult = pek::config::validateOpChainJson(opchain.dump());
    opchain["ops"][0] = {{"id", "pek-future-ops/Inference"},
                         {"attributes", {{"modelDescriptor", embeddedNull}}}};
    const auto modelDescriptorResult = pek::config::validateOpChainJson(opchain.dump());

    ASSERT_FALSE(modelResult);
    ASSERT_FALSE(opIdResult);
    ASSERT_FALSE(modelDescriptorResult);
    EXPECT_TRUE(hasRule(modelResult.error(), "model.v1.model-file-control"));
    EXPECT_TRUE(hasRule(opIdResult.error(), "opchain.v1.op-id-control"));
    EXPECT_TRUE(hasRule(modelDescriptorResult.error(), "opchain.v1.model-descriptor-control"));
}

} // namespace
