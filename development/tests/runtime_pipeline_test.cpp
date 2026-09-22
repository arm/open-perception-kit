/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "runtime/OpChain.h"
#include "runtime/Pipeline.h"
#include "runtime/VideoFrame.h"

namespace {

constexpr const char *kEnv = "OPK_RUNTIME_PIPELINE_TEST_VALUE";
constexpr const char *kRequiredEnv = "OPK_RUNTIME_PIPELINE_TEST_REQUIRED";
constexpr const char *kSpacedRootEnv = "OPK_RUNTIME_PIPELINE_TEST_SPACED_ROOT";

class TemporaryWavFile {
  public:
    TemporaryWavFile() {
        const auto uniqueSuffix =
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        path =
            std::filesystem::temp_directory_path() / ("opk_runtime_loop_" + uniqueSuffix + ".wav");

        constexpr std::uint32_t sampleRate = 8'000;
        constexpr std::uint32_t sampleCount = 800;
        std::ofstream out(path, std::ios::binary);
        auto writeLe16 = [&out](std::uint16_t value) {
            out.put(static_cast<char>(value & 0xff));
            out.put(static_cast<char>((value >> 8) & 0xff));
        };
        auto writeLe32 = [&out](std::uint32_t value) {
            out.put(static_cast<char>(value & 0xff));
            out.put(static_cast<char>((value >> 8) & 0xff));
            out.put(static_cast<char>((value >> 16) & 0xff));
            out.put(static_cast<char>((value >> 24) & 0xff));
        };

        out.write("RIFF", 4);
        writeLe32(36 + sampleCount);
        out.write("WAVEfmt ", 8);
        writeLe32(16);
        writeLe16(1);
        writeLe16(1);
        writeLe32(sampleRate);
        writeLe32(sampleRate);
        writeLe16(1);
        writeLe16(8);
        out.write("data", 4);
        writeLe32(sampleCount);
        for (std::uint32_t sample = 0; sample < sampleCount; ++sample) {
            out.put(static_cast<char>(0x80));
        }
    }

    ~TemporaryWavFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    TemporaryWavFile(const TemporaryWavFile &) = delete;
    TemporaryWavFile &operator=(const TemporaryWavFile &) = delete;

    std::filesystem::path path;
};

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
        std::filesystem::temp_directory_path() / "opk_runtime_pipeline_placeholder_test.json";
    std::ofstream out(path);
    out << contents;
    return path;
}

void expectPipelineSuccess(const opk::runtime::Result<opk::runtime::Pipeline> &result) {
    if (!result) {
        ADD_FAILURE() << result.error().toString();
    }
}

class RuntimeLoggingGuard {
  public:
    RuntimeLoggingGuard()
        : level_(opk::runtime::getLogLevel()), targets_(opk::runtime::getEnabledLogTargets()) {}

    ~RuntimeLoggingGuard() {
        for (const auto target : {opk::runtime::LogTarget::Stdout,
                                  opk::runtime::LogTarget::Stderr,
                                  opk::runtime::LogTarget::File}) {
            (void)opk::runtime::setLogTargetState(target, false);
        }
        for (const auto target : targets_)
            (void)opk::runtime::setLogTargetState(target, true);
        opk::runtime::setLogLevel(level_);
    }

    RuntimeLoggingGuard(const RuntimeLoggingGuard &) = delete;
    RuntimeLoggingGuard &operator=(const RuntimeLoggingGuard &) = delete;

  private:
    opk::runtime::LogLevel level_;
    std::vector<opk::runtime::LogTarget> targets_;
};

bool logTargetIsEnabled(opk::runtime::LogTarget expected) {
    const auto targets = opk::runtime::getEnabledLogTargets();
    return std::ranges::find(targets, expected) != targets.end();
}

} // namespace

TEST(RuntimePipelinePlaceholders, DefaultValueIsUsedWhenVariableIsUnsetOrEmpty) {
    unsetEnv(kEnv);
    auto unsetResult = opk::runtime::Pipeline::fromString(
        "fakesrc num-buffers=${OPK_RUNTIME_PIPELINE_TEST_VALUE:-1} ! fakesink");
    expectPipelineSuccess(unsetResult);

    setEnv(kEnv, "");
    auto emptyResult = opk::runtime::Pipeline::fromString(
        "fakesrc num-buffers=${OPK_RUNTIME_PIPELINE_TEST_VALUE:-1} ! fakesink");
    expectPipelineSuccess(emptyResult);

    unsetEnv(kEnv);
}

TEST(RuntimePipelinePlaceholders, RequiredValueErrorsWhenVariableIsUnsetOrEmpty) {
    unsetEnv(kRequiredEnv);
    auto unsetResult =
        opk::runtime::Pipeline::fromString("${OPK_RUNTIME_PIPELINE_TEST_REQUIRED?source element is "
                                           "required} num-buffers=1 ! fakesink");
    ASSERT_FALSE(unsetResult.has_value());
    EXPECT_EQ(unsetResult.error().flag, opk::runtime::ErrorFlag::InvalidPipeline);
    EXPECT_NE(unsetResult.error().info.find("source element is required"), std::string::npos);

    setEnv(kRequiredEnv, "");
    auto emptyResult =
        opk::runtime::Pipeline::fromString("${OPK_RUNTIME_PIPELINE_TEST_REQUIRED?source element is "
                                           "required} num-buffers=1 ! fakesink");
    ASSERT_FALSE(emptyResult.has_value());
    EXPECT_EQ(emptyResult.error().flag, opk::runtime::ErrorFlag::InvalidPipeline);
    EXPECT_NE(emptyResult.error().info.find("source element is required"), std::string::npos);

    setEnv(kRequiredEnv, "fakesrc");
    auto setResult =
        opk::runtime::Pipeline::fromString("${OPK_RUNTIME_PIPELINE_TEST_REQUIRED?source element is "
                                           "required} num-buffers=1 ! fakesink");
    expectPipelineSuccess(setResult);

    unsetEnv(kRequiredEnv);
}

TEST(RuntimePipelinePlaceholders, UnterminatedPlaceholdersReturnParseError) {
    auto defaultResult = opk::runtime::Pipeline::fromString(
        "fakesrc num-buffers=${OPK_RUNTIME_PIPELINE_TEST_VALUE:-1 ! fakesink");
    ASSERT_FALSE(defaultResult.has_value());
    EXPECT_EQ(defaultResult.error().flag, opk::runtime::ErrorFlag::ParseError);

    auto requiredResult = opk::runtime::Pipeline::fromString(
        "${OPK_RUNTIME_PIPELINE_TEST_REQUIRED?missing source num-buffers=1 ! fakesink");
    ASSERT_FALSE(requiredResult.has_value());
    EXPECT_EQ(requiredResult.error().flag, opk::runtime::ErrorFlag::ParseError);
}

TEST(RuntimePipelineJsonLoading, ExpandsPlaceholdersAfterJoiningPipelineFragments) {
    unsetEnv(kEnv);
    const auto path = writeTempPipelineJson(R"json({
        "version": "1.0.0",
        "description": "Placeholder test",
        "pipeline": [
            "fakesrc num-buffers=${OPK_RUNTIME_PIPELINE_TEST_VALUE:-1} !",
            "fakesink"
        ]
    })json");

    auto result = opk::runtime::Pipeline::fromJsonFile(path.string());
    expectPipelineSuccess(result);
    std::filesystem::remove(path);
}

TEST(RuntimePipelineJsonLoading, PreservesQuotedExpandedPathsContainingSpaces) {
    setEnv(kSpacedRootEnv, "/tmp/opk runtime checkout");
    const auto path = writeTempPipelineJson(R"json({
        "version": "1.0.0",
        "description": "Quoted path test",
        "pipeline": [
            "filesrc location=\"${OPK_RUNTIME_PIPELINE_TEST_SPACED_ROOT:-/work}/data/example.mov\" !",
            "fakesink"
        ]
    })json");

    auto result = opk::runtime::Pipeline::fromJsonFile(path.string());
    unsetEnv(kSpacedRootEnv);
    std::filesystem::remove(path);
    expectPipelineSuccess(result);
}

TEST(RuntimePipelineJsonLoading, RejectsInvalidSharedPresetMetadata) {
    const auto path = writeTempPipelineJson(R"json({
        "version": "1.0.0",
        "description": "Invalid metadata test",
        "loop": "yes",
        "pipeline": "fakesrc ! fakesink"
    })json");

    auto result = opk::runtime::Pipeline::fromJsonFile(path.string());
    std::filesystem::remove(path);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().flag, opk::runtime::ErrorFlag::InvalidPipeline);
    EXPECT_NE(result.error().info.find("/loop"), std::string::npos);
}

TEST(RuntimePipelineJsonLoading, RejectsVersionsBeforeExpandingOrCreatingElements) {
    unsetEnv(kRequiredEnv);
    for (const auto *version :
         {"null", "true", "1", "\"1\"", "1.0", "0", "-1", "2", "\"0.9.0\"", "\"2.0.0\""}) {
        const auto path = writeTempPipelineJson(
            std::string("{\"version\":") + version +
            R"(,"description":"Invalid version","pipeline":"${OPK_RUNTIME_PIPELINE_TEST_REQUIRED?should not expand}"})");
        const auto result = opk::runtime::Pipeline::fromJsonFile(path.string());
        std::filesystem::remove(path);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().flag, opk::runtime::ErrorFlag::InvalidPipeline);
        EXPECT_NE(result.error().info.find("/version"), std::string::npos);
        EXPECT_NE(result.error().info.find("supported version: 1.0.0"), std::string::npos);
        EXPECT_EQ(result.error().info.find("should not expand"), std::string::npos);
    }
}

TEST(RuntimePipelineJsonLoading, AcceptsCompatibleMinorAndPatchVersions) {
    for (const auto *version : {"1.0.57", "1.3.8"}) {
        const auto path = writeTempPipelineJson(
            std::string("{\"version\":\"") + version +
            R"(","description":"Version test","pipeline":"fakesrc num-buffers=1 ! fakesink"})");
        const auto result = opk::runtime::Pipeline::fromJsonFile(path.string());
        std::filesystem::remove(path);
        ASSERT_TRUE(result) << result.error().toString();
    }
}

TEST(RuntimePipelineBus, LogsStandardQosMessages) {
    RuntimeLoggingGuard loggingGuard;
    auto pipelineResult = opk::runtime::Pipeline::fromString(
        "videotestsrc num-buffers=3 is-live=true ! identity sleep-time=100000 ! "
        "fakesink name=qos_sink sync=true qos=true max-lateness=0");
    ASSERT_TRUE(pipelineResult.has_value()) << pipelineResult.error().toString();

    opk::runtime::flushLog();
    testing::internal::CaptureStdout();

    const opk::runtime::Pipeline::StartOptions options{
        .logLevel = opk::runtime::LogLevel::Debug,
        .logToStdout = true,
        .logToStderr = false,
        .logToFile = false,
    };
    auto startResult = pipelineResult->start(options);
    if (!startResult) {
        ADD_FAILURE() << startResult.error().toString();
    } else if (auto waitResult = pipelineResult->wait(); !waitResult) {
        ADD_FAILURE() << waitResult.error().toString();
    }
    opk::runtime::flushLog();
    const std::string output = testing::internal::GetCapturedStdout();

    EXPECT_NE(output.find("GStreamer QoS: source=qos_sink"), std::string::npos) << output;
    EXPECT_NE(output.find("format=buffers"), std::string::npos);
    EXPECT_NE(output.find("dropped="), std::string::npos);
}

TEST(RuntimeVideoFrameValidation, RejectsInvalidBgraInputs) {
    const std::vector<std::uint8_t> pixel(4U, 0U);

    EXPECT_FALSE(opk::runtime::VideoFrame::copyBgra(nullptr, 4U, 1U, 1U).has_value());
    EXPECT_FALSE(opk::runtime::VideoFrame::borrowBgra(nullptr, 4U, 1U, 1U).has_value());
    EXPECT_FALSE(
        opk::runtime::VideoFrame::borrowBgra(pixel.data(), pixel.size(), 0U, 1U).has_value());
    EXPECT_FALSE(opk::runtime::VideoFrame::borrowBgra(pixel.data(), 3U, 1U, 1U).has_value());
    EXPECT_FALSE(
        opk::runtime::VideoFrame::moveBgra(std::vector<std::uint8_t>{}, 0U, 1U).has_value());
    EXPECT_FALSE(
        opk::runtime::VideoFrame::moveBgra(
            std::vector<std::uint8_t>{}, std::numeric_limits<std::size_t>::max() / 4U + 1U, 1U)
            .has_value());
    EXPECT_FALSE(opk::runtime::VideoFrame::moveBgra(
                     std::vector<std::uint8_t>{},
                     static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1U,
                     1U)
                     .has_value());
    EXPECT_FALSE(
        opk::runtime::VideoFrame::moveBgra(std::vector<std::uint8_t>(4U), 1U, 0U).has_value());
    EXPECT_FALSE(
        opk::runtime::VideoFrame::moveBgra(std::vector<std::uint8_t>(4U), 1U, 1U, 3U).has_value());
    EXPECT_FALSE(
        opk::runtime::VideoFrame::moveBgra(std::vector<std::uint8_t>(3U), 1U, 1U).has_value());
}

TEST(RuntimePipelineErrors, RejectsInvalidStateAndDescriptions) {
    opk::runtime::Pipeline pipeline;
    EXPECT_FALSE(pipeline.start().has_value());
    EXPECT_FALSE(pipeline.pause().has_value());
    EXPECT_FALSE(pipeline.stop().has_value());
    EXPECT_FALSE(pipeline.wait().has_value());
    EXPECT_FALSE(pipeline.attachFrameResultsProbe("missing").has_value());
    EXPECT_FALSE(opk::runtime::Pipeline::addPluginPath("/does-not-exist/opk-plugins").has_value());
    EXPECT_FALSE(opk::runtime::Pipeline::fromString("opk-element-that-does-not-exist").has_value());
    EXPECT_FALSE(
        opk::runtime::Pipeline::fromJsonFile("/does-not-exist/opk-pipeline.json").has_value());

    const std::vector<std::string> invalidJson{
        "[]",
        "{}",
        R"({"pipeline":""})",
        R"({"pipeline":42})",
        R"({"pipeline":[42]})",
        R"({"pipeline":[]})",
        "{",
    };
    for (const auto &contents : invalidJson) {
        const auto path = writeTempPipelineJson(contents);
        EXPECT_FALSE(opk::runtime::Pipeline::fromJsonFile(path.string()).has_value()) << contents;
        std::filesystem::remove(path);
    }
}

TEST(RuntimeOpChainErrors, RejectsMissingChain) {
    opk::runtime::OpChain chain;
    const opk::runtime::VideoFrame frame;

    EXPECT_FALSE(chain.run(frame).has_value());
    EXPECT_FALSE(chain.runPacket(frame).has_value());
    EXPECT_FALSE(
        opk::runtime::OpChain::fromJsonFile("/does-not-exist/opk-opchain.json").has_value());
}

TEST(RuntimePipelinePlayback, DefaultStartOptionsConfigureProcessLogging) {
    RuntimeLoggingGuard loggingGuard;
    opk::runtime::setLogLevel(opk::runtime::LogLevel::Debug);
    (void)opk::runtime::setLogTargetState(opk::runtime::LogTarget::Stdout, true);
    (void)opk::runtime::setLogTargetState(opk::runtime::LogTarget::Stderr, false);
    (void)opk::runtime::setLogTargetState(opk::runtime::LogTarget::File, true);

    auto pipelineResult = opk::runtime::Pipeline::fromString("fakesrc num-buffers=1 ! fakesink");
    ASSERT_TRUE(pipelineResult.has_value()) << pipelineResult.error().toString();

    const opk::runtime::Pipeline::StartOptions defaults;
    EXPECT_FALSE(defaults.loop);
    EXPECT_EQ(defaults.logLevel, opk::runtime::LogLevel::Error);
    EXPECT_FALSE(defaults.logToStdout);
    EXPECT_TRUE(defaults.logToStderr);
    EXPECT_FALSE(defaults.logToFile);

    auto startResult = pipelineResult->start();
    ASSERT_TRUE(startResult.has_value()) << startResult.error().toString();
    EXPECT_EQ(opk::runtime::getLogLevel(), opk::runtime::LogLevel::Error);
    EXPECT_FALSE(logTargetIsEnabled(opk::runtime::LogTarget::Stdout));
    EXPECT_TRUE(logTargetIsEnabled(opk::runtime::LogTarget::Stderr));
    EXPECT_FALSE(logTargetIsEnabled(opk::runtime::LogTarget::File));

    auto waitResult = pipelineResult->wait();
    EXPECT_TRUE(waitResult.has_value()) << waitResult.error().toString();
}

TEST(RuntimePipelinePlayback, ReportsProgressCallbacksWhileRunning) {
    auto pipelineResult = opk::runtime::Pipeline::fromString(
        "fakesrc num-buffers=5 ! identity sleep-time=200000 ! fakesink");
    ASSERT_TRUE(pipelineResult.has_value()) << pipelineResult.error().toString();

    std::atomic<size_t> progressCount{0};
    pipelineResult->onProgress(
        [&progressCount](const opk::runtime::Pipeline::Progress &) { ++progressCount; });

    auto startResult = pipelineResult->start();
    ASSERT_TRUE(startResult.has_value()) << startResult.error().toString();

    auto waitResult = pipelineResult->wait();
    EXPECT_TRUE(waitResult.has_value()) << waitResult.error().toString();
    EXPECT_GT(progressCount.load(), 0U);
}

TEST(RuntimePipelinePlayback, LoopsSeekableMediaWithoutReportingEos) {
    TemporaryWavFile media;
    auto pipelineResult = opk::runtime::Pipeline::fromString(
        "filesrc location=\"" + media.path.string() + "\" ! wavparse ! fakesink sync=true");
    ASSERT_TRUE(pipelineResult.has_value()) << pipelineResult.error().toString();

    std::atomic<int> eosCount = 0;
    pipelineResult->onEos([&]() { ++eosCount; });

    const opk::runtime::Pipeline::StartOptions options{.loop = true};
    auto startResult = pipelineResult->start(options);
    ASSERT_TRUE(startResult.has_value()) << startResult.error().toString();

    auto waitFuture = std::async(std::launch::async, [&]() { return pipelineResult->wait(); });
    EXPECT_EQ(waitFuture.wait_for(std::chrono::milliseconds(500)), std::future_status::timeout);
    EXPECT_EQ(eosCount.load(), 0);

    auto stopResult = pipelineResult->stop();
    EXPECT_TRUE(stopResult.has_value()) << stopResult.error().toString();
    ASSERT_EQ(waitFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto waitResult = waitFuture.get();
    EXPECT_TRUE(waitResult.has_value()) << waitResult.error().toString();
}

TEST(RuntimePipelinePlayback, LoopsFiniteBranchBesideIndependentLiveBranch) {
    TemporaryWavFile media;
    const auto uniqueSuffix =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto outputPath = std::filesystem::temp_directory_path() /
                            ("opk_runtime_mixed_loop_" + uniqueSuffix + ".raw");
    const std::string description =
        "filesrc location=\"" + media.path.string() +
        "\" ! wavparse ! "
        "filesink append=true buffer-mode=unbuffered location=\"" +
        outputPath.string() +
        "\" audiotestsrc is-live=true wave=silence ! fakesink sync=false async=false";

    auto pipelineResult = opk::runtime::Pipeline::fromString(description);
    ASSERT_TRUE(pipelineResult.has_value()) << pipelineResult.error().toString();

    const opk::runtime::Pipeline::StartOptions options{.loop = true};
    auto startResult = pipelineResult->start(options);
    ASSERT_TRUE(startResult.has_value()) << startResult.error().toString();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    auto stopResult = pipelineResult->stop();
    ASSERT_TRUE(stopResult.has_value()) << stopResult.error().toString();
    ASSERT_TRUE(std::filesystem::exists(outputPath));

    constexpr std::uintmax_t bytesPerIteration = 800;
    EXPECT_GT(std::filesystem::file_size(outputPath), bytesPerIteration);
    std::filesystem::remove(outputPath);
}

TEST(RuntimePipelinePlayback, RejectsLoopingForANonSeekablePipeline) {
    auto pipelineResult = opk::runtime::Pipeline::fromString("fakesrc num-buffers=1 ! fakesink");
    ASSERT_TRUE(pipelineResult.has_value()) << pipelineResult.error().toString();

    const opk::runtime::Pipeline::StartOptions options{.loop = true};
    auto result = pipelineResult->start(options);
    if (result)
        result = pipelineResult->wait();

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().flag, opk::runtime::ErrorFlag::NotSupported);
    EXPECT_NE(result.error().info.find("seekable"), std::string::npos);
}

TEST(RuntimePipelinePlayback, RejectsLoopingForLivePipelineWithoutHanging) {
    auto pipelineResult = opk::runtime::Pipeline::fromString(
        "audiotestsrc is-live=true wave=silence ! fakesink sync=false");
    ASSERT_TRUE(pipelineResult.has_value()) << pipelineResult.error().toString();

    const opk::runtime::Pipeline::StartOptions options{.loop = true};
    auto startResult = pipelineResult->start(options);
    ASSERT_TRUE(startResult.has_value()) << startResult.error().toString();

    auto waitFuture = std::async(std::launch::async, [&]() { return pipelineResult->wait(); });
    ASSERT_EQ(waitFuture.wait_for(std::chrono::seconds(8)), std::future_status::ready);
    auto waitResult = waitFuture.get();

    ASSERT_FALSE(waitResult.has_value());
    EXPECT_EQ(waitResult.error().flag, opk::runtime::ErrorFlag::NotSupported);
    EXPECT_NE(waitResult.error().info.find("seekable"), std::string::npos);
}
