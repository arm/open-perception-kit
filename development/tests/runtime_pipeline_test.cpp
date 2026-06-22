/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "runtime/Pipeline.h"

namespace {

constexpr const char *kEnv = "PEK_RUNTIME_PIPELINE_TEST_VALUE";
constexpr const char *kRequiredEnv = "PEK_RUNTIME_PIPELINE_TEST_REQUIRED";

void setEnv(const char *key, const char *value) {
#ifdef _WIN32
    _putenv_s(key, value);
#else
    setenv(key, value, 1);
#endif
}

void unsetEnv(const char *key) {
#ifdef _WIN32
    _putenv_s(key, "");
#else
    unsetenv(key);
#endif
}

std::filesystem::path writeTempPipelineJson(const std::string &contents) {
    const auto path =
        std::filesystem::temp_directory_path() / "pek_runtime_pipeline_placeholder_test.json";
    std::ofstream out(path);
    out << contents;
    return path;
}

void expectPipelineSuccess(const pek::runtime::Result<pek::runtime::Pipeline> &result) {
    if (!result) {
        ADD_FAILURE() << result.error().toString();
    }
}

} // namespace

TEST(RuntimePipelinePlaceholders, DefaultValueIsUsedWhenVariableIsUnsetOrEmpty) {
    unsetEnv(kEnv);
    auto unsetResult = pek::runtime::Pipeline::fromString(
        "fakesrc num-buffers=${PEK_RUNTIME_PIPELINE_TEST_VALUE:-1} ! fakesink");
    expectPipelineSuccess(unsetResult);

    setEnv(kEnv, "");
    auto emptyResult = pek::runtime::Pipeline::fromString(
        "fakesrc num-buffers=${PEK_RUNTIME_PIPELINE_TEST_VALUE:-1} ! fakesink");
    expectPipelineSuccess(emptyResult);

    unsetEnv(kEnv);
}

TEST(RuntimePipelinePlaceholders, RequiredValueErrorsWhenVariableIsUnsetOrEmpty) {
    unsetEnv(kRequiredEnv);
    auto unsetResult =
        pek::runtime::Pipeline::fromString("${PEK_RUNTIME_PIPELINE_TEST_REQUIRED?source element is "
                                           "required} num-buffers=1 ! fakesink");
    ASSERT_FALSE(unsetResult.has_value());
    EXPECT_EQ(unsetResult.error().flag, pek::runtime::ErrorFlag::InvalidPipeline);
    EXPECT_NE(unsetResult.error().info.find("source element is required"), std::string::npos);

    setEnv(kRequiredEnv, "");
    auto emptyResult =
        pek::runtime::Pipeline::fromString("${PEK_RUNTIME_PIPELINE_TEST_REQUIRED?source element is "
                                           "required} num-buffers=1 ! fakesink");
    ASSERT_FALSE(emptyResult.has_value());
    EXPECT_EQ(emptyResult.error().flag, pek::runtime::ErrorFlag::InvalidPipeline);
    EXPECT_NE(emptyResult.error().info.find("source element is required"), std::string::npos);

    setEnv(kRequiredEnv, "fakesrc");
    auto setResult =
        pek::runtime::Pipeline::fromString("${PEK_RUNTIME_PIPELINE_TEST_REQUIRED?source element is "
                                           "required} num-buffers=1 ! fakesink");
    expectPipelineSuccess(setResult);

    unsetEnv(kRequiredEnv);
}

TEST(RuntimePipelinePlaceholders, UnterminatedPlaceholdersReturnParseError) {
    auto defaultResult = pek::runtime::Pipeline::fromString(
        "fakesrc num-buffers=${PEK_RUNTIME_PIPELINE_TEST_VALUE:-1 ! fakesink");
    ASSERT_FALSE(defaultResult.has_value());
    EXPECT_EQ(defaultResult.error().flag, pek::runtime::ErrorFlag::ParseError);

    auto requiredResult = pek::runtime::Pipeline::fromString(
        "${PEK_RUNTIME_PIPELINE_TEST_REQUIRED?missing source num-buffers=1 ! fakesink");
    ASSERT_FALSE(requiredResult.has_value());
    EXPECT_EQ(requiredResult.error().flag, pek::runtime::ErrorFlag::ParseError);
}

TEST(RuntimePipelineJsonLoading, ExpandsPlaceholdersAfterJoiningPipelineFragments) {
    unsetEnv(kEnv);
    const auto path = writeTempPipelineJson(R"json({
        "pipeline": [
            "fakesrc num-buffers=${PEK_RUNTIME_PIPELINE_TEST_VALUE:-1} !",
            "fakesink"
        ]
    })json");

    auto result = pek::runtime::Pipeline::fromJsonFile(path.string());
    expectPipelineSuccess(result);
    std::filesystem::remove(path);
}
