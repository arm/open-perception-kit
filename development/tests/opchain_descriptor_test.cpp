/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <utility>

#include "op/OpChain.h"
#include "op/OpChainDescriptor.h"

namespace {

nlohmann::json descriptor(std::string modelDescriptor,
                          std::string id = "pek-future-ops/Inference") {
    return {
        {"version", 1},
        {"name", "test"},
        {"description", "Path resolution test."},
        {"ops",
         {{{"id", std::move(id)},
           {"attributes", {{"modelDescriptor", std::move(modelDescriptor)}}}}}},
    };
}

} // namespace

TEST(OpChainDescriptor, ResolvesModelDescriptorRelativeToSource) {
    const auto directory = std::filesystem::path("pek_opchain_descriptor_test") / "opchains";
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

    const auto result = pek::op::OpChainDescriptor::fromFile(descriptorPath.string());

    ASSERT_TRUE(result.has_value());
    const auto resolved = result->ops[0].attributes.getString("modelDescriptor");
    EXPECT_EQ(resolved, (directory / "linked/../model.json").string());
    std::string marker;
    std::ifstream(resolved) >> marker;
    EXPECT_EQ(marker, "target");
    std::filesystem::remove_all(directory.parent_path());
}

TEST(OpChainDescriptor, DefaultInstanceIdsUseStablePerOpOccurrences) {
    EXPECT_EQ(pek::op::makeDefaultInstanceId("pek-python-ops/PythonScript", 0),
              "pek-python-ops-PythonScript-0");
    EXPECT_EQ(pek::op::makeDefaultInstanceId("pek-python-ops/PythonScript", 1),
              "pek-python-ops-PythonScript-1");
    EXPECT_EQ(pek::op::makeDefaultInstanceId("other/Operation", 0), "other-Operation-0");
    EXPECT_EQ(pek::op::makeDefaultInstanceId(".custom/Operation", 0), "op-.custom-Operation-0");
    EXPECT_EQ(pek::op::makeDefaultInstanceId("\xc3\xa9/Operation", 0), "op----Operation-0");
}

TEST(OpChainDescriptor, DoesNotResolveInferenceLikeCustomOpAttributes) {
    const auto directory = std::filesystem::path("pek_opchain_descriptor_test") / "custom";
    std::filesystem::remove_all(directory.parent_path());
    std::filesystem::create_directories(directory);
    const auto descriptorPath = directory / "opchain.json";
    std::ofstream(descriptorPath) << descriptor("model.json", "custom/InferenceLike");

    const auto result = pek::op::OpChainDescriptor::fromFile(descriptorPath.string());

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ops[0].attributes.getString("modelDescriptor"), "model.json");
    std::filesystem::remove_all(directory.parent_path());
}

TEST(OpChainDescriptor, PreservesCanonicalJsonAndAbsolutePaths) {
    const auto absolute = std::filesystem::absolute("models/example/model.json").string();
    const auto relative = pek::op::OpChainDescriptor::fromJson(descriptor("model.json").dump());
    const auto absoluteResult = pek::op::OpChainDescriptor::fromJson(descriptor(absolute).dump());

    const auto directory = std::filesystem::path("pek_opchain_descriptor_test") / "absolute";
    std::filesystem::remove_all(directory.parent_path());
    std::filesystem::create_directories(directory);
    const auto descriptorPath = directory / "opchain.json";
    std::ofstream(descriptorPath) << descriptor(absolute);
    const auto fileBacked = pek::op::OpChainDescriptor::fromFile(descriptorPath.string());

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
        pek::op::OpChainDescriptor::fromJson(descriptor("https://example.com/model.json").dump());

    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().info.find("schema.validation"), std::string::npos);
}

TEST(OpChainDescriptor, ResolvesPythonScriptPathsRelativeToSource) {
    const auto directory = std::filesystem::path("pek_opchain_descriptor_test") / "python";
    std::filesystem::remove_all(directory.parent_path());
    std::filesystem::create_directories(directory);
    const auto descriptorPath = directory / "opchain.json";
    nlohmann::json value = {
        {"version", 1},
        {"name", "python"},
        {"description", "Python path resolution test."},
        {"ops",
         {{{"id", "pek-python-ops/PythonScript"},
           {"attributes",
            {{"script", "scripts/process.py"},
             {"pythonPaths", {"modules", std::filesystem::absolute("shared").string()}}}}}}},
    };
    std::ofstream(descriptorPath) << value;

    const auto result = pek::op::OpChainDescriptor::fromFile(descriptorPath.string());

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->ops[0].attributes.getString("script"),
              (directory / "scripts/process.py").string());
    const auto &paths = result->ops[0].attributes.getArray("pythonPaths");
    ASSERT_EQ(paths.size(), 2U);
    EXPECT_EQ(paths[0].asString(), (directory / "modules").string());
    EXPECT_EQ(paths[1].asString(), std::filesystem::absolute("shared").string());
    std::filesystem::remove_all(directory.parent_path());
}

TEST(OpChainDescriptor, SetupRejectsInvalidSemanticsBeforePluginBinding) {
    pek::op::OpChainDescriptor invalid{
        .name = "invalid loop",
        .description = "Must fail before plugin lookup.",
        .displayName = {},
        .task = {},
        .runtime = {},
        .ops = {{.id = "missing/CustomOp", .loopId = 1, .attributes = {}}},
    };
    pek::op::OpChain chain;

    const auto result = chain.setupFromDescriptor(invalid);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().flag, pek::ErrorFlag::InvalidOpChain);
    EXPECT_NE(result.error().info.find("opchain.v1.loop-group"), std::string::npos);
}
