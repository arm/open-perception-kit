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

#include <fmt/format.h>

#include "Log.h"
#include "pek/ModelDescriptor.h"

namespace fs = std::filesystem;

namespace {

constexpr const char *FakeModeEnvironment = "PEK_MODELFETCH_FAKE_MODE";
constexpr const char *FakeEscapePathEnvironment = "PEK_MODELFETCH_FAKE_ESCAPE_PATH";
constexpr const char *FakeCallsEnvironment = "PEK_MODELFETCH_FAKE_CALLS";
constexpr const char *FakeTokenModeEnvironment = "PEK_MODELFETCH_FAKE_TOKEN_MODE";
constexpr const char *FakeExpectedTokenEnvironment = "PEK_MODELFETCH_FAKE_EXPECTED_TOKEN";
constexpr const char *HuggingFaceTokenEnvironment = "HF_TOKEN";

class TemporaryDirectory {
  public:
    explicit TemporaryDirectory(const std::string &name)
        : path(fs::current_path() / fmt::format("pek-model-descriptor-{}-{}", getpid(), name)) {
        fs::remove_all(path);
        fs::create_directories(path);
    }

    TemporaryDirectory(const TemporaryDirectory &) = delete;
    TemporaryDirectory &operator=(const TemporaryDirectory &) = delete;
    TemporaryDirectory(TemporaryDirectory &&) = delete;
    TemporaryDirectory &operator=(TemporaryDirectory &&) = delete;

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

    explicit ScopedEnvironmentVariable(const char *keyValue) : key(keyValue) {
        if (const char *current = g_getenv(key); current != nullptr)
            previous = current;
        g_unsetenv(key);
    }

    ScopedEnvironmentVariable(const ScopedEnvironmentVariable &) = delete;
    ScopedEnvironmentVariable &operator=(const ScopedEnvironmentVariable &) = delete;
    ScopedEnvironmentVariable(ScopedEnvironmentVariable &&) = delete;
    ScopedEnvironmentVariable &operator=(ScopedEnvironmentVariable &&) = delete;

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
    return fmt::format("hf:Arm/example@0123456789abcdef0123456789abcdef01234567#file="
                       "model-descriptor-tests/{}-{}/model.onnx",
                       getpid(),
                       name);
}

fs::path publishedModelPath(const std::string &name) {
    return fs::path("/work/var/models/model-descriptor-tests") /
           fmt::format("{}-{}", getpid(), name) / "model.onnx";
}

class PublishedModelFixture {
  public:
    explicit PublishedModelFixture(const std::string &name)
        : modelPath(publishedModelPath(name)), directory(modelPath.parent_path()) {
        fs::create_directories(directory);
        std::ofstream(modelPath) << "model";
    }

    PublishedModelFixture(const PublishedModelFixture &) = delete;
    PublishedModelFixture &operator=(const PublishedModelFixture &) = delete;
    PublishedModelFixture(PublishedModelFixture &&) = delete;
    PublishedModelFixture &operator=(PublishedModelFixture &&) = delete;

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

std::string readFile(const fs::path &path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void startLogCapture() {
    pek::log::setLogLevel(4);
    EXPECT_TRUE(pek::log::setLogTargetState(pek::log::TargetType::Stdout, true));
    EXPECT_TRUE(pek::log::setLogTargetState(pek::log::TargetType::Stderr, false));
    EXPECT_TRUE(pek::log::setLogTargetState(pek::log::TargetType::File, false));
    pek::log::flush();
    testing::internal::CaptureStdout();
}

std::string finishLogCapture() {
    pek::log::flush();
    return testing::internal::GetCapturedStdout();
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

    startLogCapture();
    const auto descriptor = pek::ModelDescriptor::fromFile(descriptorPath.string());
    const std::string logOutput = finishLogCapture();

    ASSERT_TRUE(descriptor.has_value()) << descriptor.error().toString();
    EXPECT_EQ(descriptor->modelFile, publishedModelPath(name).string());
    EXPECT_TRUE(fs::is_regular_file(descriptor->modelFile));
    EXPECT_NE(logOutput.find("modelfetch materialized modelFile"), std::string::npos);
}

TEST(ModelDescriptor, FromFileUsesAnonymousHuggingFaceAuthWhenTokenIsUnset) {
    constexpr const char *name = "auth-unset";
    TemporaryDirectory temporary(name);
    PublishedModelFixture model(name);
    const fs::path markerPath = temporary.path / "token-mode";
    ScopedEnvironmentVariable token(HuggingFaceTokenEnvironment);
    ScopedEnvironmentVariable marker(FakeTokenModeEnvironment, markerPath.string());
    const fs::path descriptorPath = writeDescriptor(temporary.path, publishedAssetId(name));

    const auto descriptor = pek::ModelDescriptor::fromFile(descriptorPath.string());

    ASSERT_TRUE(descriptor.has_value()) << descriptor.error().toString();
    EXPECT_EQ(readFile(markerPath), "anonymous\n");
}

TEST(ModelDescriptor, FromFilePassesHuggingFaceTokenToModelfetch) {
    constexpr const char *name = "auth-token";
    constexpr const char *tokenValue = "hf_test_token";
    TemporaryDirectory temporary(name);
    PublishedModelFixture model(name);
    const fs::path markerPath = temporary.path / "token-mode";
    ScopedEnvironmentVariable token(HuggingFaceTokenEnvironment, tokenValue);
    ScopedEnvironmentVariable expectedToken(FakeExpectedTokenEnvironment, tokenValue);
    ScopedEnvironmentVariable marker(FakeTokenModeEnvironment, markerPath.string());
    const fs::path descriptorPath = writeDescriptor(temporary.path, publishedAssetId(name));

    const auto descriptor = pek::ModelDescriptor::fromFile(descriptorPath.string());

    ASSERT_TRUE(descriptor.has_value()) << descriptor.error().toString();
    EXPECT_EQ(readFile(markerPath), "explicit\n");
}

TEST(ModelDescriptor, FromFileLeavesHuggingFaceTokenValidationToModelfetch) {
    TemporaryDirectory temporary("empty-token");
    ScopedEnvironmentVariable token(HuggingFaceTokenEnvironment, "");
    const fs::path descriptorPath =
        writeDescriptor(temporary.path, publishedAssetId("empty-token"));

    const auto descriptor = pek::ModelDescriptor::fromFile(descriptorPath.string());

    ASSERT_FALSE(descriptor.has_value());
    EXPECT_NE(descriptor.error().info.find("modelfetch asset download failed"), std::string::npos);
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

    startLogCapture();
    const auto descriptor = pek::ModelDescriptor::fromFile(descriptorPath.string());
    const std::string logOutput = finishLogCapture();

    EXPECT_FALSE(descriptor.has_value());
    EXPECT_EQ(logOutput.find("modelfetch materialized modelFile"), std::string::npos);
}

TEST(ModelDescriptor, FromFileRejectsInvalidIntegrityToken) {
    constexpr const char *name = "invalid-integrity";
    TemporaryDirectory temporary(name);
    PublishedModelFixture model(name);
    ScopedEnvironmentVariable mode(FakeModeEnvironment, name);
    const fs::path descriptorPath = writeDescriptor(temporary.path, publishedAssetId(name));

    EXPECT_FALSE(pek::ModelDescriptor::fromFile(descriptorPath.string()).has_value());
}
