/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <utility>

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
