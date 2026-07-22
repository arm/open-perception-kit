/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unistd.h>

#include <glib.h>

#include "pek/ModelDescriptor.h"

namespace fs = std::filesystem;

namespace {

constexpr const char *FakeModeEnvironment = "PEK_MODELFETCH_FAKE_MODE";
constexpr const char *FakeEscapePathEnvironment = "PEK_MODELFETCH_FAKE_ESCAPE_PATH";

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

class ScopedEnvironmentVariable {
  public:
    ScopedEnvironmentVariable(const char *keyValue, const std::string &value) : key(keyValue) {
        if (const char *current = g_getenv(key); current != nullptr)
            previous = current;
        g_setenv(key, value.c_str(), TRUE);
    }

    ~ScopedEnvironmentVariable() {
        if (previous)
            g_setenv(key, previous->c_str(), TRUE);
        else
            g_unsetenv(key);
    }

  private:
    const char *key;
    std::optional<std::string> previous;
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

std::string publishedAssetId(const std::string &name) {
    return "hf:Arm/example@0123456789abcdef0123456789abcdef01234567#file=" +
           std::string("model-descriptor-tests/") + std::to_string(getpid()) + "-" + name +
           "/model.onnx";
}

fs::path publishedModelPath(const std::string &name) {
    return fs::path("/work/var/models/model-descriptor-tests") /
           (std::to_string(getpid()) + "-" + name) / "model.onnx";
}

class PublishedModelFixture {
  public:
    explicit PublishedModelFixture(const std::string &name)
        : modelPath(publishedModelPath(name)), directory(modelPath.parent_path()) {
        fs::create_directories(directory);
        std::ofstream(modelPath) << "model";
    }
    ~PublishedModelFixture() {
        std::error_code ec;
        fs::remove_all(directory, ec);
    }

  private:
    fs::path modelPath;
    fs::path directory;
};

} // namespace

TEST(ModelDescriptor, ModelFileRoundTrips) {
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
}

TEST(ModelDescriptor, FromFileResolvesLocalModelBesideDescriptor) {
    TemporaryDirectory temporary("local");
    const fs::path descriptorPath = writeDescriptor(temporary.path, "weights/model.onnx");

    const auto descriptor = pek::ModelDescriptor::fromFile(descriptorPath.string());

    ASSERT_TRUE(descriptor.has_value());
    EXPECT_EQ(descriptor->modelFile, (temporary.path / "weights/model.onnx").string());
}

TEST(ModelDescriptor, FromFileMaterializesPublishedModelInDedicatedStore) {
    constexpr const char *name = "downloaded";
    TemporaryDirectory temporary(name);
    PublishedModelFixture model(name);
    ScopedEnvironmentVariable mode(FakeModeEnvironment, "downloaded");
    const fs::path descriptorPath = writeDescriptor(temporary.path, publishedAssetId(name));

    const auto descriptor = pek::ModelDescriptor::fromFile(descriptorPath.string());

    ASSERT_TRUE(descriptor.has_value()) << descriptor.error().toString();
    EXPECT_EQ(descriptor->modelFile, publishedModelPath(name).string());
    EXPECT_TRUE(fs::is_regular_file(descriptor->modelFile));
}

TEST(ModelDescriptor, FromFileAcceptsExistingVerifiedPublishedModel) {
    constexpr const char *name = "existing";
    TemporaryDirectory temporary(name);
    PublishedModelFixture model(name);
    ScopedEnvironmentVariable mode(FakeModeEnvironment, "existing");
    const fs::path descriptorPath = writeDescriptor(temporary.path, publishedAssetId(name));

    const auto descriptor = pek::ModelDescriptor::fromFile(descriptorPath.string());

    ASSERT_TRUE(descriptor.has_value()) << descriptor.error().toString();
    EXPECT_EQ(descriptor->modelFile, publishedModelPath(name).string());
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

TEST(ModelDescriptor, FromFileRejectsMutablePublishedRevision) {
    TemporaryDirectory temporary("mutable-revision");
    const fs::path descriptorPath =
        writeDescriptor(temporary.path, "hf:Arm/example@main#file=model.onnx");

    EXPECT_FALSE(pek::ModelDescriptor::fromFile(descriptorPath.string()).has_value());
}

TEST(ModelDescriptor, FromFileRejectsModelfetchApiFailure) {
    TemporaryDirectory temporary("api-failure");
    ScopedEnvironmentVariable mode(FakeModeEnvironment, "api-failure");
    const fs::path descriptorPath =
        writeDescriptor(temporary.path, publishedAssetId("api-failure"));

    EXPECT_FALSE(pek::ModelDescriptor::fromFile(descriptorPath.string()).has_value());
}

TEST(ModelDescriptor, FromFileRejectsModelfetchFailureOutcome) {
    TemporaryDirectory temporary("failure-outcome");
    ScopedEnvironmentVariable mode(FakeModeEnvironment, "failure-outcome");
    const fs::path descriptorPath =
        writeDescriptor(temporary.path, publishedAssetId("failure-outcome"));

    EXPECT_FALSE(pek::ModelDescriptor::fromFile(descriptorPath.string()).has_value());
}

TEST(ModelDescriptor, FromFileRejectsMismatchedModelfetchAsset) {
    constexpr const char *name = "mismatched-asset";
    TemporaryDirectory temporary(name);
    PublishedModelFixture model(name);
    ScopedEnvironmentVariable mode(FakeModeEnvironment, name);
    const fs::path descriptorPath = writeDescriptor(temporary.path, publishedAssetId(name));

    EXPECT_FALSE(pek::ModelDescriptor::fromFile(descriptorPath.string()).has_value());
}

TEST(ModelDescriptor, FromFileRejectsInvalidUtf8FromModelfetch) {
    constexpr const char *name = "invalid-utf8";
    TemporaryDirectory temporary(name);
    PublishedModelFixture model(name);
    ScopedEnvironmentVariable mode(FakeModeEnvironment, name);
    const fs::path descriptorPath = writeDescriptor(temporary.path, publishedAssetId(name));

    EXPECT_FALSE(pek::ModelDescriptor::fromFile(descriptorPath.string()).has_value());
}

TEST(ModelDescriptor, FromFileRejectsMultipleModelfetchResults) {
    constexpr const char *name = "multiple-results";
    TemporaryDirectory temporary(name);
    PublishedModelFixture model(name);
    ScopedEnvironmentVariable mode(FakeModeEnvironment, name);
    const fs::path descriptorPath = writeDescriptor(temporary.path, publishedAssetId(name));

    EXPECT_FALSE(pek::ModelDescriptor::fromFile(descriptorPath.string()).has_value());
}

TEST(ModelDescriptor, FromFileRejectsMultipleMaterializedPaths) {
    constexpr const char *name = "multiple-paths";
    TemporaryDirectory temporary(name);
    PublishedModelFixture model(name);
    ScopedEnvironmentVariable mode(FakeModeEnvironment, name);
    const fs::path descriptorPath = writeDescriptor(temporary.path, publishedAssetId(name));

    EXPECT_FALSE(pek::ModelDescriptor::fromFile(descriptorPath.string()).has_value());
}

TEST(ModelDescriptor, FromFileRejectsMaterializedPathOutsideStore) {
    constexpr const char *name = "escape";
    TemporaryDirectory temporary(name);
    ScopedEnvironmentVariable mode(FakeModeEnvironment, name);
    ScopedEnvironmentVariable escapePath(FakeEscapePathEnvironment,
                                         (temporary.path / "escaped.onnx").string());
    const fs::path descriptorPath = writeDescriptor(temporary.path, publishedAssetId(name));

    EXPECT_FALSE(pek::ModelDescriptor::fromFile(descriptorPath.string()).has_value());
}

TEST(ModelDescriptor, FromFileRejectsInvalidIntegrityToken) {
    constexpr const char *name = "invalid-integrity";
    TemporaryDirectory temporary(name);
    PublishedModelFixture model(name);
    ScopedEnvironmentVariable mode(FakeModeEnvironment, name);
    const fs::path descriptorPath = writeDescriptor(temporary.path, publishedAssetId(name));

    EXPECT_FALSE(pek::ModelDescriptor::fromFile(descriptorPath.string()).has_value());
}
