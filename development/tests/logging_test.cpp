/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <optional>
#include <string>

#include "pek/Log.h"

namespace {

class ScopedEnv {
  public:
    ScopedEnv(const char *name, const char *value) : name_(name) {
        const char *existing = std::getenv(name); // NOLINT(concurrency-mt-unsafe)
        if (existing != nullptr) {
            previous_ = existing;
        }

        setenv(name, value, 1); // NOLINT(concurrency-mt-unsafe)
    }

    ~ScopedEnv() {
        if (previous_) {
            setenv(name_.c_str(), previous_->c_str(), 1); // NOLINT(concurrency-mt-unsafe)
        } else {
            unsetenv(name_.c_str()); // NOLINT(concurrency-mt-unsafe)
        }
    }

  private:
    std::string name_;
    std::optional<std::string> previous_;
};

} // namespace

TEST(PekLog, OrdersLevelsByVerbosityAndDefaultsToInfo) {
    EXPECT_EQ(pek::private_::logLevelValue(pek::LogLevel::Off), 0);
    EXPECT_EQ(pek::private_::logLevelValue(pek::LogLevel::Error), 1);
    EXPECT_EQ(pek::private_::logLevelValue(pek::LogLevel::Warn), 2);
    EXPECT_EQ(pek::private_::logLevelValue(pek::LogLevel::Notice), 3);
    EXPECT_EQ(pek::private_::logLevelValue(pek::LogLevel::Info), 4);
    EXPECT_EQ(pek::defaultLogLevel, pek::LogLevel::Info);
}

TEST(PekLog, AppliesSeverityThresholdAtEveryConfiguredLevel) {
    struct ExpectedThresholds {
        pek::LogLevel configuredLevel;
        bool error;
        bool warning;
        bool notice;
        bool info;
    };

    constexpr std::array expectations{
        ExpectedThresholds{pek::LogLevel::Off, false, false, false, false},
        ExpectedThresholds{pek::LogLevel::Error, true, false, false, false},
        ExpectedThresholds{pek::LogLevel::Warn, true, true, false, false},
        ExpectedThresholds{pek::LogLevel::Notice, true, true, true, false},
        ExpectedThresholds{pek::LogLevel::Info, true, true, true, true},
    };

    for (const auto &expectation : expectations) {
        SCOPED_TRACE(pek::private_::logLevelValue(expectation.configuredLevel));
        pek::setLogLevel(pek::private_::logLevelValue(expectation.configuredLevel));

        EXPECT_EQ(pek::private_::shouldLog(pek::LogLevel::Error), expectation.error);
        EXPECT_EQ(pek::private_::shouldLog(pek::LogLevel::Warn), expectation.warning);
        EXPECT_EQ(pek::private_::shouldLog(pek::LogLevel::Notice), expectation.notice);
        EXPECT_EQ(pek::private_::shouldLog(pek::LogLevel::Info), expectation.info);
    }
}

TEST(PekLog, WritesInfoAndFlushes) {
    ScopedEnv env("OPK_LOG_LEVEL", "4");
    pek::setLogLevel(4);
    testing::internal::CaptureStdout();

    pek::log("hello {}", "world");
    pek::logFlush();

    EXPECT_EQ(testing::internal::GetCapturedStdout(), "hello world");
}

TEST(PekLog, AppliesWarningAndErrorPrefixes) {
    ScopedEnv env("OPK_LOG_LEVEL", "4");
    pek::setLogLevel(4);
    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();

    pek::logw("warn {}\n", 1);
    pek::loge("err {}\n", 2);

    const std::string stdoutOutput = testing::internal::GetCapturedStdout();
    const std::string stderrOutput = testing::internal::GetCapturedStderr();
    EXPECT_EQ(stdoutOutput, "W: warn 1\nE: err 2\n");
    EXPECT_EQ(stderrOutput, "W: warn 1\nE: err 2\n");
}

TEST(PekLog, FiltersNoticeAndInfoMessagesAtWarningLevel) {
    ScopedEnv env("OPK_LOG_LEVEL", "2");
    pek::setLogLevel(2);
    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();

    pek::log("info\n");
    pek::logn("notice\n");
    pek::logw("warn\n");
    pek::loge("error\n");

    const std::string stdoutOutput = testing::internal::GetCapturedStdout();
    const std::string stderrOutput = testing::internal::GetCapturedStderr();
    EXPECT_EQ(stdoutOutput, "W: warn\nE: error\n");
    EXPECT_EQ(stderrOutput, "W: warn\nE: error\n");
}

TEST(PekLog, WritesNoticesAtLevelThree) {
    ScopedEnv env("OPK_LOG_LEVEL", "3");
    pek::setLogLevel(3);
    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();

    pek::log("info\n");
    pek::logn("notice\n");
    pek::logw("warn\n");
    pek::loge("error\n");

    const std::string stdoutOutput = testing::internal::GetCapturedStdout();
    const std::string stderrOutput = testing::internal::GetCapturedStderr();
    EXPECT_EQ(stdoutOutput, "\033[7mnotice\n\033[0mW: warn\nE: error\n");
    EXPECT_EQ(stderrOutput, "W: warn\nE: error\n");
}

TEST(PekLog, FiltersWarningsAtErrorLevel) {
    ScopedEnv env("OPK_LOG_LEVEL", "1");
    pek::setLogLevel(1);
    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();

    pek::logw("warn\n");
    pek::loge("error\n");

    const std::string stdoutOutput = testing::internal::GetCapturedStdout();
    const std::string stderrOutput = testing::internal::GetCapturedStderr();
    EXPECT_EQ(stdoutOutput, "E: error\n");
    EXPECT_EQ(stderrOutput, "E: error\n");
}

TEST(PekLog, WritesUnconditionalOutputToOneStream) {
    ScopedEnv env("OPK_LOG_LEVEL", "0");
    pek::setLogLevel(0);
    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();

    pek::forceLog("stdout {}\n", 1);
    pek::forceLoge("stderr {}\n", 2);

    const std::string stdoutOutput = testing::internal::GetCapturedStdout();
    const std::string stderrOutput = testing::internal::GetCapturedStderr();
    EXPECT_EQ(stdoutOutput, "stdout 1\n");
    EXPECT_EQ(stderrOutput, "stderr 2\n");
}

TEST(PekLog, UpdatesLogLevelWithoutChangingEnvironment) {
    ScopedEnv env("OPK_LOG_LEVEL", "1");
    pek::setLogLevel(3);

    const char *configuredLevel = std::getenv("OPK_LOG_LEVEL"); // NOLINT(concurrency-mt-unsafe)
    EXPECT_STREQ(configuredLevel, "1");
    EXPECT_EQ(pek::getLogLevel(), 3);

    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();
    pek::log("info\n");
    pek::logn("notice\n");
    pek::logw("warn\n");
    pek::loge("error\n");

    const std::string stdoutOutput = testing::internal::GetCapturedStdout();
    const std::string stderrOutput = testing::internal::GetCapturedStderr();
    EXPECT_EQ(stdoutOutput, "\033[7mnotice\n\033[0mW: warn\nE: error\n");
    EXPECT_EQ(stderrOutput, "W: warn\nE: error\n");
}

TEST(PekLog, ClampsLogLevelToSupportedRange) {
    pek::setLogLevel(-1);
    EXPECT_EQ(pek::getLogLevel(), 0);

    pek::setLogLevel(5);
    EXPECT_EQ(pek::getLogLevel(), 4);
    EXPECT_EQ(pek::private_::parseLogLevel("5"), 4);
}
