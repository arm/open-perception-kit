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

#include <filesystem>
#include <fstream>
#include <utility>

#include "op/OpChain.h"
#include "op/OpChainDescriptor.h"

namespace {

nlohmann::json descriptor(std::string modelDescriptor,
                          std::string id = "opk-future-ops/Inference") {
    return {
        {"version", "1.0.0"},
        {"name", "test"},
        {"description", "Path resolution test."},
        {"ops",
         {{{"id", std::move(id)},
           {"attributes", {{"modelDescriptor", std::move(modelDescriptor)}}}}}},
    };
}

} // namespace

TEST(OpChainDescriptor, ResolvesModelDescriptorRelativeToSource) {
    const auto directory = std::filesystem::path("opk_opchain_descriptor_test") / "opchains";
    std::filesystem::remove_all(directory.parent_path());
    std::filesystem::create_directories(directory);
    const auto targetDirectory = directory.parent_path() / "model-target";
    std::filesystem::create_directories(targetDirectory / "child");
    std::filesystem::create_directory_symlink(std::filesystem::absolute(targetDirectory / "child"),
                                              directory / "linked");
    std::ofstream(targetDirectory / "model.json") << "target";
    std::ofstream(directory / "model.json") << "lexically-normalized";
    const auto descriptorPath = directory / "opchain.json";
    std::ofstream(descriptorPath) << descriptor("linked/../model.json");

    const auto result = opk::op::OpChainDescriptor::fromFile(descriptorPath.string());

    ASSERT_TRUE(result.has_value());
    const auto resolved = result->ops[0].attributes.getString("modelDescriptor");
    EXPECT_EQ(resolved, (directory / "linked/../model.json").string());
    std::string marker;
    std::ifstream(resolved) >> marker;
    EXPECT_EQ(marker, "target");
    std::filesystem::remove_all(directory.parent_path());
}

TEST(OpChainDescriptor, DefaultInstanceIdsUseStablePerOpOccurrences) {
    EXPECT_EQ(opk::op::makeDefaultInstanceId("opk-python-ops/PythonScript", 0),
              "opk-python-ops-PythonScript-0");
    EXPECT_EQ(opk::op::makeDefaultInstanceId("opk-python-ops/PythonScript", 1),
              "opk-python-ops-PythonScript-1");
    EXPECT_EQ(opk::op::makeDefaultInstanceId("other/Operation", 0), "other-Operation-0");
    EXPECT_EQ(opk::op::makeDefaultInstanceId(".custom/Operation", 0), "op-.custom-Operation-0");
    EXPECT_EQ(opk::op::makeDefaultInstanceId("\xc3\xa9/Operation", 0), "op----Operation-0");
}

TEST(OpChainDescriptor, DoesNotResolveInferenceLikeCustomOpAttributes) {
    const auto directory = std::filesystem::path("opk_opchain_descriptor_test") / "custom";
    std::filesystem::remove_all(directory.parent_path());
    std::filesystem::create_directories(directory);
    const auto descriptorPath = directory / "opchain.json";
    std::ofstream(descriptorPath) << descriptor("model.json", "custom/InferenceLike");

    const auto result = opk::op::OpChainDescriptor::fromFile(descriptorPath.string());

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ops[0].attributes.getString("modelDescriptor"), "model.json");
    std::filesystem::remove_all(directory.parent_path());
}

TEST(OpChainDescriptor, PreservesCanonicalJsonAndAbsolutePaths) {
    const auto absolute = std::filesystem::absolute("models/example/model.json").string();
    const auto relative = opk::op::OpChainDescriptor::fromJson(descriptor("model.json").dump());
    const auto absoluteResult = opk::op::OpChainDescriptor::fromJson(descriptor(absolute).dump());

    const auto directory = std::filesystem::path("opk_opchain_descriptor_test") / "absolute";
    std::filesystem::remove_all(directory.parent_path());
    std::filesystem::create_directories(directory);
    const auto descriptorPath = directory / "opchain.json";
    std::ofstream(descriptorPath) << descriptor(absolute);
    const auto fileBacked = opk::op::OpChainDescriptor::fromFile(descriptorPath.string());

    ASSERT_TRUE(relative.has_value());
    EXPECT_EQ(relative->ops[0].attributes.getString("modelDescriptor"), "model.json");
    ASSERT_TRUE(absoluteResult.has_value());
    EXPECT_EQ(absoluteResult->ops[0].attributes.getString("modelDescriptor"), absolute);
    ASSERT_TRUE(fileBacked.has_value());
    EXPECT_EQ(fileBacked->ops[0].attributes.getString("modelDescriptor"), absolute);
    std::filesystem::remove_all(directory.parent_path());
}

TEST(OpChainDescriptor, RejectsUriModelDescriptor) {
    const auto result =
        opk::op::OpChainDescriptor::fromJson(descriptor("https://example.com/model.json").dump());

    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().info.find("schema.validation"), std::string::npos);
}

TEST(OpChainDescriptor, ResolvesPythonScriptPathsRelativeToModelDescriptor) {
    const auto root = std::filesystem::path("opk_opchain_descriptor_test") / "python";
    const auto opchainDirectory = root / "opchains";
    const auto modelDirectory = root / "models" / "example";
    std::filesystem::remove_all(root.parent_path());
    std::filesystem::create_directories(opchainDirectory);
    std::filesystem::create_directories(modelDirectory);
    const auto descriptorPath = opchainDirectory / "opchain.json";
    nlohmann::json value = {
        {"version", "1.0.0"},
        {"name", "python"},
        {"description", "Python path resolution test."},
        {"ops",
         {{{"id", "opk-future-ops/Inference"},
           {"attributes", {{"modelDescriptor", "../models/example/model.json"}}}},
          {{"id", "opk-python-ops/PythonScript"},
           {"attributes",
            {{"script", "scripts/process.py"},
             {"pythonPaths", {"modules", std::filesystem::absolute("shared").string()}}}}}}},
    };
    std::ofstream(descriptorPath) << value;

    const auto result = opk::op::OpChainDescriptor::fromFile(descriptorPath.string());

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ops[1].attributes.getString("script"),
              (opchainDirectory / "../models/example/scripts/process.py").string());
    const auto &paths = result->ops[1].attributes.getArray("pythonPaths");
    ASSERT_EQ(paths.size(), 2U);
    EXPECT_EQ(paths[0].asString(), (opchainDirectory / "../models/example/modules").string());
    EXPECT_EQ(paths[1].asString(), std::filesystem::absolute("shared").string());
    std::filesystem::remove_all(root.parent_path());
}

TEST(OpChainDescriptor, RejectsRelativePythonScriptPathsWithoutModelDescriptor) {
    const auto directory = std::filesystem::path("opk_opchain_descriptor_test") / "python-no-model";
    std::filesystem::remove_all(directory.parent_path());
    std::filesystem::create_directories(directory);
    const auto descriptorPath = directory / "opchain.json";
    nlohmann::json value = {
        {"version", "1.0.0"},
        {"name", "python"},
        {"description", "Python path resolution test."},
        {"ops",
         {{{"id", "opk-python-ops/PythonScript"},
           {"attributes", {{"script", "scripts/process.py"}}}}}},
    };
    std::ofstream(descriptorPath) << value;

    const auto result = opk::op::OpChainDescriptor::fromFile(descriptorPath.string());

    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().info.find("require an inference modelDescriptor"), std::string::npos);
    std::filesystem::remove_all(directory.parent_path());
}

TEST(OpChainDescriptor, RejectsAmbiguousRelativePythonScriptPaths) {
    const auto directory =
        std::filesystem::path("opk_opchain_descriptor_test") / "python-ambiguous";
    std::filesystem::remove_all(directory.parent_path());
    std::filesystem::create_directories(directory);
    const auto descriptorPath = directory / "opchain.json";
    nlohmann::json value = {
        {"version", "1.0.0"},
        {"name", "python"},
        {"description", "Python path resolution test."},
        {"ops",
         {{{"id", "opk-future-ops/Inference"},
           {"attributes", {{"modelDescriptor", "models/first/model.json"}}}},
          {{"id", "opk-future-ops/Inference"},
           {"attributes", {{"modelDescriptor", "models/second/model.json"}}}},
          {{"id", "opk-python-ops/PythonScript"},
           {"attributes", {{"script", "scripts/process.py"}}}}}},
    };
    std::ofstream(descriptorPath) << value;

    const auto result = opk::op::OpChainDescriptor::fromFile(descriptorPath.string());

    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().info.find("ambiguous"), std::string::npos);
    std::filesystem::remove_all(directory.parent_path());
}

TEST(OpChainDescriptor, AllowsAbsolutePythonScriptPathsWithMultipleModelDirectories) {
    const auto directory = std::filesystem::path("opk_opchain_descriptor_test") / "python-absolute";
    std::filesystem::remove_all(directory.parent_path());
    std::filesystem::create_directories(directory);
    const auto descriptorPath = directory / "opchain.json";
    const auto absoluteScript = std::filesystem::absolute("scripts/process.py").string();
    const auto absolutePythonPath = std::filesystem::absolute("modules").string();
    nlohmann::json value = {
        {"version", "1.0.0"},
        {"name", "python"},
        {"description", "Python path resolution test."},
        {"ops",
         {{{"id", "opk-future-ops/Inference"},
           {"attributes", {{"modelDescriptor", "models/first/model.json"}}}},
          {{"id", "opk-future-ops/Inference"},
           {"attributes", {{"modelDescriptor", "models/second/model.json"}}}},
          {{"id", "opk-python-ops/PythonScript"},
           {"attributes", {{"script", absoluteScript}, {"pythonPaths", {absolutePythonPath}}}}}}},
    };
    std::ofstream(descriptorPath) << value;

    const auto result = opk::op::OpChainDescriptor::fromFile(descriptorPath.string());

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ops[2].attributes.getString("script"), absoluteScript);
    const auto &paths = result->ops[2].attributes.getArray("pythonPaths");
    ASSERT_EQ(paths.size(), 1U);
    EXPECT_EQ(paths[0].asString(), absolutePythonPath);
    std::filesystem::remove_all(directory.parent_path());
}

TEST(OpChainDescriptor, SetupRejectsInvalidSemanticsBeforePluginBinding) {
    opk::op::OpChainDescriptor invalid{
        .name = "invalid loop",
        .description = "Must fail before plugin lookup.",
        .displayName = {},
        .task = {},
        .runtime = {},
        .ops = {{.id = "missing/CustomOp", .loopId = 1, .attributes = {}}},
    };
    opk::op::OpChain chain;

    const auto result = chain.setupFromDescriptor(invalid);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().flag, opk::ErrorFlag::InvalidOpChain);
    EXPECT_NE(result.error().info.find("opchain.v1.loop-group"), std::string::npos);
}
