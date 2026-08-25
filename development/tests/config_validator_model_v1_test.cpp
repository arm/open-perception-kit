/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "config_validator_test_support.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace {

using pek::config::test::hasRule;

nlohmann::json staticModel() {
    return nlohmann::json{
        {"version", 1},
        {"name", "feedback-test"},
        {"modelFile", "model.onnx"},
        {"dynamicOutput", false},
        {"inputTensors",
         {{{"shape", {1}}, {"dataKind", "RawTensorData"}, {"valueType", "Float32"}}}},
        {"outputTensors",
         {{{"shape", {1}}, {"dataKind", "RawTensorData"}, {"valueType", "Float32"}}}}};
}

TEST(ConfigValidator, ModelV1RejectsTensorByteSizeOverflow) {
    auto document = staticModel();
    document["inputTensors"][0]["shape"] = {2147483647, 2147483647, 2147483647, 2147483647};

    const auto result = pek::config::validateModelJson(document.dump());

    ASSERT_FALSE(result);
    EXPECT_TRUE(hasRule(result.error(), "model.v1.tensor-size"));
}

TEST(ConfigValidator, ModelV1ValidatesFeedbackDestination) {
    auto document = staticModel();
    document["inputTensors"].push_back(
        {{"dataKind", "Value"}, {"valueInputs", {0.0}}, {"valueType", "Float32"}});
    document["tensorFeedbacks"] = {
        {{"mode", "Copy"}, {"fromOutputTensorIndex", 0}, {"toInputTensorIndex", 1}},
        {{"mode", "Copy"}, {"fromOutputTensorIndex", 0}, {"toInputTensorIndex", 2}},
    };

    const auto invalidDestination = pek::config::validateModelJson(document.dump());

    ASSERT_FALSE(invalidDestination);
    EXPECT_TRUE(hasRule(invalidDestination.error(), "model.v1.feedback-destination"));

    document["tensorFeedbacks"] = {
        {{"mode", "Copy"}, {"fromOutputTensorIndex", 0}, {"toInputTensorIndex", 0}},
        {{"mode", "Copy"}, {"fromOutputTensorIndex", 1}, {"toInputTensorIndex", 0}},
    };
    const auto duplicateDestination = pek::config::validateModelJson(document.dump());
    ASSERT_FALSE(duplicateDestination);
    EXPECT_TRUE(hasRule(duplicateDestination.error(), "model.v1.feedback-destination"));
}

TEST(ConfigValidator, ModelV1ValidatesStaticFeedbackSourceAndCompatibility) {
    auto document = staticModel();
    document["tensorFeedbacks"] = {
        {{"mode", "Copy"}, {"fromOutputTensorIndex", 1}, {"toInputTensorIndex", 0}},
    };

    const auto missingSource = pek::config::validateModelJson(document.dump());
    ASSERT_FALSE(missingSource);
    EXPECT_TRUE(hasRule(missingSource.error(), "model.v1.feedback-source"));

    document["tensorFeedbacks"][0]["fromOutputTensorIndex"] = 0;
    document["outputTensors"][0]["shape"] = {2};
    const auto incompatible = pek::config::validateModelJson(document.dump());
    ASSERT_FALSE(incompatible);
    EXPECT_TRUE(hasRule(incompatible.error(), "model.v1.feedback-compatible"));

    document["outputTensors"][0]["shape"] = {1};
    const auto valid = pek::config::validateModelJson(document.dump());
    EXPECT_TRUE(valid) << (valid ? "" : valid.error().toText());
}

TEST(ConfigValidator, ModelV1DefersDynamicFeedbackSourceValidation) {
    auto document = staticModel();
    document["dynamicOutput"] = true;
    document.erase("outputTensors");
    document["tensorFeedbacks"] = {
        {{"mode", "Copy"}, {"fromOutputTensorIndex", 15}, {"toInputTensorIndex", 0}},
    };

    const auto result = pek::config::validateModelJson(document.dump());

    EXPECT_TRUE(result) << (result ? "" : result.error().toText());
}

} // namespace
