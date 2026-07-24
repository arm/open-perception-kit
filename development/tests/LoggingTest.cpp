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
        pek::log::setLogLevel(4);
        ASSERT_TRUE(pek::log::setLogTargetState(pek::log::LogTargetType::Stdout, true));
        ASSERT_TRUE(pek::log::setLogTargetState(pek::log::LogTargetType::Stderr, false));
        pek::log::logFlush();
    }

    void TearDown() override {
        pek::log::logFlush();
    }
};

} // namespace

TEST(PekLog, OrdersLevelsByVerbosityAndDefaultsToInfo) {
    EXPECT_EQ(static_cast<int>(pek::log::LogLevel::Off), 0);
    EXPECT_EQ(static_cast<int>(pek::log::LogLevel::Error), 1);
    EXPECT_EQ(static_cast<int>(pek::log::LogLevel::Warn), 2);
    EXPECT_EQ(static_cast<int>(pek::log::LogLevel::Notice), 3);
    EXPECT_EQ(static_cast<int>(pek::log::LogLevel::Info), 4);
    EXPECT_EQ(pek::log::defaultLogLevel, pek::log::LogLevel::Info);
}

TEST(LogTools, InvertsText) {
    EXPECT_EQ(pek::log::LogTools::invert("notice\n"), "\033[7mnotice\n\033[0m");
}

TEST_F(PekLogTest, AppliesSeverityThresholdAtEveryConfiguredLevel) {
    struct ExpectedOutput {
        pek::log::LogLevel configuredLevel;
        std::string output;
    };

    const std::array expectations{
        ExpectedOutput{pek::log::LogLevel::Off, ""},
        ExpectedOutput{pek::log::LogLevel::Error, "E: error\n"},
        ExpectedOutput{pek::log::LogLevel::Warn, "W: warning\nE: error\n"},
        ExpectedOutput{pek::log::LogLevel::Notice,
                       pek::log::LogTools::invert("notice\n") + "W: warning\nE: error\n"},
        ExpectedOutput{pek::log::LogLevel::Info,
                       "info\n" + pek::log::LogTools::invert("notice\n") +
                           "W: warning\nE: error\n"},
    };

    for (const auto &expectation : expectations) {
        const int configuredLevel = static_cast<int>(expectation.configuredLevel);
        SCOPED_TRACE(configuredLevel);
        pek::log::setLogLevel(configuredLevel);
        testing::internal::CaptureStdout();

        pek::log::info("info\n");
        pek::log::notice("notice\n");
        pek::log::warning("warning\n");
        pek::log::error("error\n");
        pek::log::logFlush();

        EXPECT_EQ(testing::internal::GetCapturedStdout(), expectation.output);
    }
}

TEST_F(PekLogTest, WritesEverySeverityToEveryEnabledTarget) {
    ASSERT_TRUE(pek::log::setLogTargetState(pek::log::LogTargetType::Stderr, true));
    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();

    pek::log::info("info\n");
    pek::log::notice("notice\n");
    pek::log::warning("warn\n");
    pek::log::error("error\n");
    pek::log::logFlush();

    const std::string expected =
        "info\n" + pek::log::LogTools::invert("notice\n") + "W: warn\nE: error\n";
    EXPECT_EQ(testing::internal::GetCapturedStdout(), expected);
    EXPECT_EQ(testing::internal::GetCapturedStderr(), expected);
}

TEST_F(PekLogTest, DisablesAndEnablesAvailableTargets) {
    EXPECT_EQ(pek::log::getEnabledLogTargets(), std::vector{pek::log::LogTargetType::Stdout});
    EXPECT_TRUE(pek::log::setLogTargetState(pek::log::LogTargetType::Stdout, false));
    EXPECT_TRUE(pek::log::getEnabledLogTargets().empty());
    EXPECT_TRUE(pek::log::setLogTargetState(pek::log::LogTargetType::Stderr, true));
    EXPECT_EQ(pek::log::getEnabledLogTargets(), std::vector{pek::log::LogTargetType::Stderr});
}

TEST_F(PekLogTest, FiltersMessagesAtConfiguredLevel) {
    pek::log::setLogLevel(2);
    testing::internal::CaptureStdout();

    pek::log::info("info\n");
    pek::log::notice("notice\n");
    pek::log::warning("warn\n");
    pek::log::error("error\n");
    pek::log::logFlush();

    EXPECT_EQ(testing::internal::GetCapturedStdout(), "W: warn\nE: error\n");
}

TEST_F(PekLogTest, WritesRuntimeFormatsAsynchronously) {
    testing::internal::CaptureStdout();

    pek::log::logRuntime("runtime {}\n", 1);
    pek::log::logwRuntime("runtime {}\n", 2);
    pek::log::logeRuntime("runtime {}\n", 3);
    pek::log::logFlush();

    EXPECT_EQ(testing::internal::GetCapturedStdout(), "runtime 1\nW: runtime 2\nE: runtime 3\n");
}

TEST_F(PekLogTest, WritesUnconditionalOutputToOneStream) {
    pek::log::setLogLevel(0);
    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();

    pek::log::instantInfo("stdout {}\n", 1);
    pek::log::instantError("stderr {}\n", 2);

    EXPECT_EQ(testing::internal::GetCapturedStdout(), "stdout 1\n");
    EXPECT_EQ(testing::internal::GetCapturedStderr(), "stderr 2\n");
}

TEST_F(PekLogTest, ClampsLogLevelToSupportedRange) {
    pek::log::setLogLevel(-1);
    EXPECT_EQ(pek::log::getLogLevel(), 0);

    pek::log::setLogLevel(5);
    EXPECT_EQ(pek::log::getLogLevel(), 4);
}
