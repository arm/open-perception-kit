/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "Log.h"
#include "LogTools.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

class PekLogTest : public testing::Test {
  protected:
    void SetUp() override {
        pek::setLogLevel(4);
        ASSERT_TRUE(pek::setLogTargetState(pek::LogTargetType::Stdout, true));
        ASSERT_TRUE(pek::setLogTargetState(pek::LogTargetType::Stderr, false));
        pek::logFlush();
    }

    void TearDown() override {
        pek::logFlush();
    }
};

} // namespace

TEST(PekLog, OrdersLevelsByVerbosityAndDefaultsToInfo) {
    EXPECT_EQ(static_cast<int>(pek::LogLevel::Off), 0);
    EXPECT_EQ(static_cast<int>(pek::LogLevel::Error), 1);
    EXPECT_EQ(static_cast<int>(pek::LogLevel::Warn), 2);
    EXPECT_EQ(static_cast<int>(pek::LogLevel::Notice), 3);
    EXPECT_EQ(static_cast<int>(pek::LogLevel::Info), 4);
    EXPECT_EQ(pek::defaultLogLevel, pek::LogLevel::Info);
}

TEST(LogTools, InvertsText) {
    EXPECT_EQ(pek::LogTools::invert("notice\n"), "\033[7mnotice\n\033[0m");
}

TEST_F(PekLogTest, AppliesSeverityThresholdAtEveryConfiguredLevel) {
    struct ExpectedOutput {
        pek::LogLevel configuredLevel;
        std::string output;
    };

    const std::array expectations{
        ExpectedOutput{pek::LogLevel::Off, ""},
        ExpectedOutput{pek::LogLevel::Error, "E: error\n"},
        ExpectedOutput{pek::LogLevel::Warn, "W: warning\nE: error\n"},
        ExpectedOutput{pek::LogLevel::Notice,
                       pek::LogTools::invert("notice\n") + "W: warning\nE: error\n"},
        ExpectedOutput{pek::LogLevel::Info,
                       "info\n" + pek::LogTools::invert("notice\n") + "W: warning\nE: error\n"},
    };

    for (const auto &expectation : expectations) {
        const int configuredLevel = static_cast<int>(expectation.configuredLevel);
        SCOPED_TRACE(configuredLevel);
        pek::setLogLevel(configuredLevel);
        testing::internal::CaptureStdout();

        pek::log("info\n");
        pek::logn("notice\n");
        pek::logw("warning\n");
        pek::loge("error\n");
        pek::logFlush();

        EXPECT_EQ(testing::internal::GetCapturedStdout(), expectation.output);
    }
}

TEST_F(PekLogTest, WritesEverySeverityToEveryEnabledTarget) {
    ASSERT_TRUE(pek::setLogTargetState(pek::LogTargetType::Stderr, true));
    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();

    pek::log("info\n");
    pek::logn("notice\n");
    pek::logw("warn\n");
    pek::loge("error\n");
    pek::logFlush();

    const std::string expected =
        "info\n" + pek::LogTools::invert("notice\n") + "W: warn\nE: error\n";
    EXPECT_EQ(testing::internal::GetCapturedStdout(), expected);
    EXPECT_EQ(testing::internal::GetCapturedStderr(), expected);
}

TEST_F(PekLogTest, DisablesAndEnablesAvailableTargets) {
    EXPECT_EQ(pek::getEnabledLogTargets(), std::vector{pek::LogTargetType::Stdout});
    EXPECT_TRUE(pek::setLogTargetState(pek::LogTargetType::Stdout, false));
    EXPECT_TRUE(pek::getEnabledLogTargets().empty());
    EXPECT_TRUE(pek::setLogTargetState(pek::LogTargetType::Stderr, true));
    EXPECT_EQ(pek::getEnabledLogTargets(), std::vector{pek::LogTargetType::Stderr});
}

TEST_F(PekLogTest, FiltersMessagesAtConfiguredLevel) {
    pek::setLogLevel(2);
    testing::internal::CaptureStdout();

    pek::log("info\n");
    pek::logn("notice\n");
    pek::logw("warn\n");
    pek::loge("error\n");
    pek::logFlush();

    EXPECT_EQ(testing::internal::GetCapturedStdout(), "W: warn\nE: error\n");
}

TEST_F(PekLogTest, WritesRuntimeFormatsAsynchronously) {
    testing::internal::CaptureStdout();

    pek::logRuntime("runtime {}\n", 1);
    pek::logwRuntime("runtime {}\n", 2);
    pek::logeRuntime("runtime {}\n", 3);
    pek::logFlush();

    EXPECT_EQ(testing::internal::GetCapturedStdout(), "runtime 1\nW: runtime 2\nE: runtime 3\n");
}

TEST_F(PekLogTest, WritesUnconditionalOutputToOneStream) {
    pek::setLogLevel(0);
    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();

    pek::forceLog("stdout {}\n", 1);
    pek::forceLoge("stderr {}\n", 2);

    EXPECT_EQ(testing::internal::GetCapturedStdout(), "stdout 1\n");
    EXPECT_EQ(testing::internal::GetCapturedStderr(), "stderr 2\n");
}

TEST_F(PekLogTest, ClampsLogLevelToSupportedRange) {
    pek::setLogLevel(-1);
    EXPECT_EQ(pek::getLogLevel(), 0);

    pek::setLogLevel(5);
    EXPECT_EQ(pek::getLogLevel(), 4);
}
