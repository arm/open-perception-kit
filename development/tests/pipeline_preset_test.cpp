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

#include <cstdlib>
#include <string>

#include "opk/PipelinePreset.h"

namespace {

constexpr const char *kEnvironmentVariable = "OPK_PIPELINE_PRESET_TEST_VALUE";

void setEnvironment(const char *value) {
#ifdef _WIN32
    _putenv_s(kEnvironmentVariable, value);
#else
    setenv(kEnvironmentVariable, value, 1);
#endif
}

void unsetEnvironment() {
#ifdef _WIN32
    _putenv_s(kEnvironmentVariable, "");
#else
    unsetenv(kEnvironmentVariable);
#endif
}

} // namespace

TEST(PipelinePresetParsing, LoadsAllLauncherMetadataAndJoinsPipelineFragments) {
    auto result = opk::PipelinePreset::fromJson(R"json({
        "version": "1.0.0",
        "description": "Test pipeline",
        "sourceInfo": "file source",
        "loop": true,
        "pipeline": ["fakesrc !", "", "fakesink"]
    })json");

    ASSERT_TRUE(result.has_value()) << result.error().info;
    ASSERT_TRUE(result->description.has_value());
    EXPECT_EQ(*result->description, "Test pipeline");
    ASSERT_TRUE(result->sourceInfo.has_value());
    EXPECT_EQ(*result->sourceInfo, "file source");
    EXPECT_EQ(result->pipeline, "fakesrc ! fakesink");
    EXPECT_TRUE(result->loop);
}

TEST(PipelinePresetParsing, RequiresVersionAndDescription) {
    auto result = opk::PipelinePreset::fromJson(R"json({
        "pipeline": "fakesrc ! fakesink"
    })json");

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().flag, opk::ErrorFlag::InvalidData);
}

TEST(PipelinePresetParsing, RejectsInvalidLauncherMetadata) {
    auto description = opk::PipelinePreset::fromJson(R"json({
        "version": "1.0.0",
        "description": 42,
        "pipeline": "fakesrc ! fakesink"
    })json");
    ASSERT_FALSE(description.has_value());
    EXPECT_EQ(description.error().flag, opk::ErrorFlag::InvalidData);

    auto loop = opk::PipelinePreset::fromJson(R"json({
        "version": "1.0.0",
        "loop": "yes",
        "pipeline": "fakesrc ! fakesink"
    })json");
    ASSERT_FALSE(loop.has_value());
    EXPECT_EQ(loop.error().flag, opk::ErrorFlag::InvalidData);

    auto source_info = opk::PipelinePreset::fromJson(R"json({
        "version": "1.0.0",
        "sourceInfo": 42,
        "pipeline": "fakesrc ! fakesink"
    })json");
    ASSERT_FALSE(source_info.has_value());
    EXPECT_EQ(source_info.error().flag, opk::ErrorFlag::InvalidData);
}

TEST(PipelinePresetExpansion, SupportsValueDefaultAndRequiredPlaceholders) {
    unsetEnvironment();
    auto fallback =
        opk::expandPipelineDescription("source=${OPK_PIPELINE_PRESET_TEST_VALUE:-default}");
    ASSERT_TRUE(fallback.has_value()) << fallback.error().info;
    EXPECT_EQ(*fallback, "source=default");

    auto required = opk::expandPipelineDescription(
        "source=${OPK_PIPELINE_PRESET_TEST_VALUE?value is required}");
    ASSERT_FALSE(required.has_value());
    EXPECT_EQ(required.error().flag, opk::ErrorFlag::InvalidData);
    EXPECT_EQ(required.error().info, "value is required");

    setEnvironment("configured value");
    auto configured = opk::expandPipelineDescription("source=${OPK_PIPELINE_PRESET_TEST_VALUE}");
    unsetEnvironment();

    ASSERT_TRUE(configured.has_value()) << configured.error().info;
    EXPECT_EQ(*configured, "source=configured value");
}

TEST(PipelinePresetExpansion, ReportsUnterminatedPlaceholders) {
    auto fallback =
        opk::expandPipelineDescription("source=${OPK_PIPELINE_PRESET_TEST_VALUE:-default");
    ASSERT_FALSE(fallback.has_value());
    EXPECT_EQ(fallback.error().flag, opk::ErrorFlag::ParseError);

    auto required =
        opk::expandPipelineDescription("source=${OPK_PIPELINE_PRESET_TEST_VALUE?value is required");
    ASSERT_FALSE(required.has_value());
    EXPECT_EQ(required.error().flag, opk::ErrorFlag::ParseError);
}
