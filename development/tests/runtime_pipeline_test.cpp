/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "Log.h"
#include "runtime/Pipeline.h"

namespace {

constexpr const char *kEnv = "PEK_RUNTIME_PIPELINE_TEST_VALUE";
constexpr const char *kRequiredEnv = "PEK_RUNTIME_PIPELINE_TEST_REQUIRED";
constexpr const char *kSpacedRootEnv = "PEK_RUNTIME_PIPELINE_TEST_SPACED_ROOT";

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

TEST(RuntimePipelineJsonLoading, PreservesQuotedExpandedPathsContainingSpaces) {
    setEnv(kSpacedRootEnv, "/tmp/pek runtime checkout");
    const auto path = writeTempPipelineJson(R"json({
        "pipeline": [
            "filesrc location=\"${PEK_RUNTIME_PIPELINE_TEST_SPACED_ROOT:-/work}/data/example.mov\" !",
            "fakesink"
        ]
    })json");

    auto result = pek::runtime::Pipeline::fromJsonFile(path.string());
    unsetEnv(kSpacedRootEnv);
    std::filesystem::remove(path);
    expectPipelineSuccess(result);
}

TEST(RuntimePipelineBus, LogsStandardQosMessages) {
    auto pipelineResult = pek::runtime::Pipeline::fromString(
        "videotestsrc num-buffers=3 is-live=true ! identity sleep-time=100000 ! "
        "fakesink name=qos_sink sync=true qos=true max-lateness=0");
    ASSERT_TRUE(pipelineResult.has_value()) << pipelineResult.error().toString();

    const int oldLevel = pek::log::getLogLevel();
    const auto oldTargets = pek::log::getEnabledLogTargets();
    pek::log::setLogLevel(5);
    pek::log::setLogTargetState(pek::log::TargetType::Stdout, true);
    pek::log::setLogTargetState(pek::log::TargetType::Stderr, false);
    pek::log::setLogTargetState(pek::log::TargetType::File, false);
    pek::log::flush();
    testing::internal::CaptureStdout();

    auto startResult = pipelineResult->start();
    if (!startResult) {
        ADD_FAILURE() << startResult.error().toString();
    } else if (auto waitResult = pipelineResult->wait(); !waitResult) {
        ADD_FAILURE() << waitResult.error().toString();
    }
    pek::log::flush();
    const std::string output = testing::internal::GetCapturedStdout();

    for (auto target :
         {pek::log::TargetType::Stdout, pek::log::TargetType::Stderr, pek::log::TargetType::File}) {
        pek::log::setLogTargetState(target, false);
    }
    for (auto target : oldTargets) {
        pek::log::setLogTargetState(target, true);
    }
    pek::log::setLogLevel(oldLevel);

    EXPECT_NE(output.find("GStreamer QoS: source=qos_sink"), std::string::npos) << output;
    EXPECT_NE(output.find("format=buffers"), std::string::npos);
    EXPECT_NE(output.find("dropped="), std::string::npos);
}
