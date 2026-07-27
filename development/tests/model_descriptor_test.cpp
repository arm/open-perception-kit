/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <stop_token>
#include <string>
#include <unistd.h>

#include <glib.h>

#include "op/OpSetupContext.h"
#include "pek/ModelDescriptor.h"

namespace fs = std::filesystem;

namespace {

constexpr const char *FakeModeEnvironment = "PEK_MODELFETCH_FAKE_MODE";
constexpr const char *FakeEscapePathEnvironment = "PEK_MODELFETCH_FAKE_ESCAPE_PATH";
constexpr const char *FakeCallsEnvironment = "PEK_MODELFETCH_FAKE_CALLS";

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

size_t countRecordedCalls(const fs::path &path) {
    std::ifstream calls(path);
    size_t count = 0;
    std::string line;
    while (std::getline(calls, line))
        ++count;
    return count;
}

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

TEST(TensorFeedback, RoundTripsCurrentSchema) {
    const pek::TensorFeedback feedback{
        .fromOutputTensorIndex = 2,
        .toInputTensorIndex = 3,
    };

    const nlohmann::json serialized = feedback;

    EXPECT_EQ(serialized,
              nlohmann::json({{"fromOutputTensorIndex", 2}, {"toInputTensorIndex", 3}}));
    const auto roundTripped = serialized.get<pek::TensorFeedback>();
    EXPECT_EQ(roundTripped.fromOutputTensorIndex, feedback.fromOutputTensorIndex);
    EXPECT_EQ(roundTripped.toInputTensorIndex, feedback.toInputTensorIndex);
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

TEST(ModelDescriptor, FromFileHonorsCancellationBeforeMaterialization) {
    constexpr const char *name = "cancelled-before-call";
    TemporaryDirectory temporary(name);
    const fs::path callsPath = temporary.path / "calls.log";
    ScopedEnvironmentVariable calls(FakeCallsEnvironment, callsPath.string());
    const fs::path descriptorPath = writeDescriptor(temporary.path, publishedAssetId(name));
    std::stop_source stopSource;
    stopSource.request_stop();

    const auto descriptor =
        pek::ModelDescriptor::fromFile(descriptorPath.string(), stopSource.get_token());

    ASSERT_FALSE(descriptor.has_value());
    EXPECT_EQ(descriptor.error().flag, pek::ErrorFlag::SystemFailure);
    EXPECT_NE(descriptor.error().info.find("cancelled"), std::string::npos);
    EXPECT_EQ(countRecordedCalls(callsPath), 0U);
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

    const auto descriptor = pek::ModelDescriptor::fromFile(descriptorPath.string());

    ASSERT_FALSE(descriptor.has_value());
    EXPECT_NE(descriptor.error().info.find("integrity_mismatch"), std::string::npos);
}

TEST(ModelDescriptor, FromFileRejectsMismatchedModelfetchAsset) {
    constexpr const char *name = "mismatched-asset";
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
    const fs::path escapedModel = temporary.path / "escaped.onnx";
    std::ofstream(escapedModel) << "model";
    ScopedEnvironmentVariable mode(FakeModeEnvironment, name);
    ScopedEnvironmentVariable escapePath(FakeEscapePathEnvironment, escapedModel.string());
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

TEST(OpSetupContext, CachesSuccessfulDescriptorResolutionWithinOneSetup) {
    constexpr const char *name = "setup-context-cache";
    TemporaryDirectory temporary(name);
    PublishedModelFixture model(name);
    ScopedEnvironmentVariable mode(FakeModeEnvironment, "downloaded");
    const fs::path callsPath = temporary.path / "calls.log";
    ScopedEnvironmentVariable calls(FakeCallsEnvironment, callsPath.string());
    const fs::path descriptorPath = writeDescriptor(temporary.path, publishedAssetId(name));
    pek::op::OpSetupContext setupContext;

    const auto first = setupContext.resolveModelDescriptor(descriptorPath.string());
    const auto second = setupContext.resolveModelDescriptor(descriptorPath.string());

    ASSERT_TRUE(first.has_value()) << first.error().toString();
    ASSERT_TRUE(second.has_value()) << second.error().toString();
    EXPECT_EQ(first->modelFile, second->modelFile);
    EXPECT_EQ(countRecordedCalls(callsPath), 1U);
}

TEST(OpSetupContext, ResolvesDistinctDescriptorsIndependently) {
    constexpr const char *firstName = "setup-context-first";
    constexpr const char *secondName = "setup-context-second";
    TemporaryDirectory temporary("setup-context-distinct");
    const fs::path firstDirectory = temporary.path / "first";
    const fs::path secondDirectory = temporary.path / "second";
    fs::create_directories(firstDirectory);
    fs::create_directories(secondDirectory);
    PublishedModelFixture firstModel(firstName);
    PublishedModelFixture secondModel(secondName);
    ScopedEnvironmentVariable mode(FakeModeEnvironment, "downloaded");
    const fs::path callsPath = temporary.path / "calls.log";
    ScopedEnvironmentVariable calls(FakeCallsEnvironment, callsPath.string());
    const fs::path firstDescriptor = writeDescriptor(firstDirectory, publishedAssetId(firstName));
    const fs::path secondDescriptor =
        writeDescriptor(secondDirectory, publishedAssetId(secondName));
    pek::op::OpSetupContext setupContext;

    const auto first = setupContext.resolveModelDescriptor(firstDescriptor.string());
    const auto second = setupContext.resolveModelDescriptor(secondDescriptor.string());

    ASSERT_TRUE(first.has_value()) << first.error().toString();
    ASSERT_TRUE(second.has_value()) << second.error().toString();
    EXPECT_NE(first->modelFile, second->modelFile);
    EXPECT_EQ(countRecordedCalls(callsPath), 2U);
}

TEST(OpSetupContext, DoesNotCacheFailedDescriptorResolution) {
    TemporaryDirectory temporary("setup-context-failure");
    ScopedEnvironmentVariable mode(FakeModeEnvironment, "api-failure");
    const fs::path callsPath = temporary.path / "calls.log";
    ScopedEnvironmentVariable calls(FakeCallsEnvironment, callsPath.string());
    const fs::path descriptorPath =
        writeDescriptor(temporary.path, publishedAssetId("setup-context-failure"));
    pek::op::OpSetupContext setupContext;

    EXPECT_FALSE(setupContext.resolveModelDescriptor(descriptorPath.string()).has_value());
    EXPECT_FALSE(setupContext.resolveModelDescriptor(descriptorPath.string()).has_value());
    EXPECT_EQ(countRecordedCalls(callsPath), 2U);
}
