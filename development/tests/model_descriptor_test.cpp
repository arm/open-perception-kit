/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "pek/ModelDescriptor.h"

namespace fs = std::filesystem;

namespace {

class TemporaryDirectory {
  public:
    explicit TemporaryDirectory(const std::string &name)
        : path(fs::temp_directory_path() /
               ("pek-model-descriptor-" + std::to_string(getpid()) + "-" + name)) {
        fs::remove_all(path);
        fs::create_directories(path);
    }

    ~TemporaryDirectory() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }

    fs::path path;
};

fs::path writeDescriptor(const fs::path &directory, const std::string &modelFile) {
    const fs::path descriptorPath = directory / "model.json";
    const nlohmann::json document = {{"name", "example"},
                                     {"modelFile", modelFile},
                                     {"modelFamily", "example"},
                                     {"dynamicOutput", true}};
    std::ofstream output(descriptorPath);
    output << document.dump();
    output.close();
    return descriptorPath;
}

} // namespace

TEST(ModelDescriptor, ModelFileRoundTripsWithoutExtraSourceField) {
    constexpr const char *assetId =
        "hf:Arm/example@0123456789abcdef0123456789abcdef01234567#file=onnx/model.onnx";
    const nlohmann::json document = {{"name", "example"},
                                     {"modelFile", assetId},
                                     {"modelFamily", "example"},
                                     {"dynamicOutput", true}};

    const auto descriptor = pek::ModelDescriptor::fromJson(document.dump());

    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(descriptor->modelFile, assetId);
    const nlohmann::json serialized = *descriptor;
    EXPECT_EQ(serialized.at("modelFile"), assetId);
    EXPECT_FALSE(serialized.contains("modelAssetId"));
}

TEST(ModelDescriptor, FromFileResolvesLocalModelBesideDescriptor) {
    TemporaryDirectory temporary("local");
    const fs::path descriptorPath = writeDescriptor(temporary.path, "weights/model.onnx");

    const auto descriptor = pek::ModelDescriptor::fromFile(descriptorPath.string());

    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(descriptor->modelFile, (temporary.path / "weights/model.onnx").string());
}

TEST(ModelDescriptor, FromFileResolvesPublishedModelInDedicatedStore) {
    TemporaryDirectory temporary("published");
    constexpr const char *assetId =
        "hf:Arm/example@0123456789abcdef0123456789abcdef01234567#file=onnx/model.onnx";
    const fs::path descriptorPath = writeDescriptor(temporary.path, assetId);

    const auto descriptor = pek::ModelDescriptor::fromFile(descriptorPath.string());

    ASSERT_TRUE(descriptor.has_value());
    const fs::path resolvedPath(descriptor->modelFile);
    const fs::path storeRoot("/work/var/models");
    const fs::path relativePath = resolvedPath.lexically_relative(storeRoot);
    EXPECT_TRUE(resolvedPath.is_absolute());
    ASSERT_FALSE(relativePath.empty());
    EXPECT_FALSE(relativePath.is_absolute());
    EXPECT_NE(*relativePath.begin(), fs::path(".."));
}

TEST(ModelDescriptor, FromFileRejectsUnsafeLocalModelPath) {
    TemporaryDirectory temporary("unsafe");
    const fs::path descriptorPath = writeDescriptor(temporary.path, "../model.onnx");

    EXPECT_FALSE(pek::ModelDescriptor::fromFile(descriptorPath.string()).has_value());
}

TEST(ModelDescriptor, FromFileRejectsPublishedNonFileLocator) {
    TemporaryDirectory temporary("bundle");
    const fs::path descriptorPath = writeDescriptor(
        temporary.path, "hf:Arm/example@0123456789abcdef0123456789abcdef01234567#bundle");

    EXPECT_FALSE(pek::ModelDescriptor::fromFile(descriptorPath.string()).has_value());
}

TEST(ModelDescriptor, FromFileRejectsLocatorRejectedByModelfetch) {
    TemporaryDirectory temporary("invalid-published");
    const fs::path descriptorPath =
        writeDescriptor(temporary.path, "hf:not-canonical#file=model.onnx");

    EXPECT_FALSE(pek::ModelDescriptor::fromFile(descriptorPath.string()).has_value());
}
