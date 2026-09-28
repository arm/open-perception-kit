/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "Log.h"
#include "tools.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdlib>
#include <source_location>
#include <string>
#include <vector>

namespace {

class OpkLogTest : public testing::Test {
  protected:
    void SetUp() override {
        opk::log::setLogLevel(4);
        ASSERT_TRUE(opk::log::setLogTargetState(opk::log::TargetType::Stdout, true));
        ASSERT_TRUE(opk::log::setLogTargetState(opk::log::TargetType::Stderr, false));
        ASSERT_TRUE(opk::log::setLogTargetState(opk::log::TargetType::File, false));
        opk::log::flush();
    }

    void TearDown() override {
        opk::log::flush();
    }
};

} // namespace

TEST(OpkLog, OrdersLevelsByVerbosityAndDefaultsToInfo) {
    EXPECT_EQ(static_cast<int>(opk::log::Level::Off), 0);
    EXPECT_EQ(static_cast<int>(opk::log::Level::Error), 1);
    EXPECT_EQ(static_cast<int>(opk::log::Level::Warn), 2);
    EXPECT_EQ(static_cast<int>(opk::log::Level::Notice), 3);
    EXPECT_EQ(static_cast<int>(opk::log::Level::Info), 4);
    EXPECT_EQ(static_cast<int>(opk::log::Level::Debug), 5);
    EXPECT_EQ(opk::log::defaultLogLevel, opk::log::Level::Info);
}

TEST(tools, InvertsText) {
    EXPECT_EQ(opk::log::tools::invert("notice\n"), "\033[7mnotice\n\033[0m");
}

TEST_F(OpkLogTest, AppliesSeverityThresholdAtEveryConfiguredLevel) {
    struct ExpectedOutput {
        opk::log::Level configuredLevel;
        std::string output;
    };

    const std::array expectations{
        ExpectedOutput{opk::log::Level::Off, ""},
        ExpectedOutput{opk::log::Level::Error, "E: error\n"},
        ExpectedOutput{opk::log::Level::Warn, "W: warning\nE: error\n"},
        ExpectedOutput{opk::log::Level::Notice,
                       opk::log::tools::invert("notice\n") + "W: warning\nE: error\n"},
        ExpectedOutput{opk::log::Level::Info,
                       "info\n" + opk::log::tools::invert("notice\n") + "W: warning\nE: error\n"},
        ExpectedOutput{opk::log::Level::Debug,
                       "info\n" + opk::log::tools::invert("notice\n") + "W: warning\nE: error\n"},
    };

    for (const auto &expectation : expectations) {
        const int configuredLevel = static_cast<int>(expectation.configuredLevel);
        SCOPED_TRACE(configuredLevel);
        opk::log::setLogLevel(configuredLevel);
        testing::internal::CaptureStdout();

        opk::log::info("info\n");
        opk::log::notice("notice\n");
        opk::log::warning("warning\n");
        opk::log::error("error\n");
        const auto debugLine = std::source_location::current().line() + 1;
        opk::log::debug("debug");
        opk::log::flush();

        auto expected = expectation.output;
        if (expectation.configuredLevel == opk::log::Level::Debug) {
            expected += std::string(opk::log::color::BrightCyan) +
                        "[LoggingTest.cpp:" + std::to_string(debugLine) + "] " +
                        std::string(opk::log::color::ResetColor) + "debug\n";
        }
        EXPECT_EQ(testing::internal::GetCapturedStdout(), expected);
    }
}

TEST_F(OpkLogTest, WritesEverySeverityToEveryEnabledTarget) {
    opk::log::setLogLevel(5);
    ASSERT_TRUE(opk::log::setLogTargetState(opk::log::TargetType::Stderr, true));
    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();

    opk::log::info("info\n");
    opk::log::notice("notice\n");
    opk::log::warning("warn\n");
    opk::log::error("error\n");
    const auto debugLine = std::source_location::current().line() + 1;
    opk::log::debug("debug");
    opk::log::flush();

    const std::string expected = "info\n" + opk::log::tools::invert("notice\n") +
                                 "W: warn\nE: error\n" + std::string(opk::log::color::BrightCyan) +
                                 "[LoggingTest.cpp:" + std::to_string(debugLine) + "] " +
                                 std::string(opk::log::color::ResetColor) + "debug\n";
    EXPECT_EQ(testing::internal::GetCapturedStdout(), expected);
    EXPECT_EQ(testing::internal::GetCapturedStderr(), expected);
}

TEST_F(OpkLogTest, DisablesAndEnablesAvailableTargets) {
    EXPECT_EQ(opk::log::getEnabledLogTargets(), std::vector{opk::log::TargetType::Stdout});
    EXPECT_TRUE(opk::log::setLogTargetState(opk::log::TargetType::Stdout, false));
    EXPECT_TRUE(opk::log::getEnabledLogTargets().empty());
    EXPECT_TRUE(opk::log::setLogTargetState(opk::log::TargetType::Stderr, true));
    EXPECT_EQ(opk::log::getEnabledLogTargets(), std::vector{opk::log::TargetType::Stderr});
}

TEST_F(OpkLogTest, FiltersMessagesAtConfiguredLevel) {
    opk::log::setLogLevel(2);
    testing::internal::CaptureStdout();

    opk::log::info("info\n");
    opk::log::notice("notice\n");
    opk::log::warning("warn\n");
    opk::log::error("error\n");
    opk::log::flush();

    EXPECT_EQ(testing::internal::GetCapturedStdout(), "W: warn\nE: error\n");
}

TEST_F(OpkLogTest, WritesDebugWithSourceLocationAsOneRecord) {
    opk::log::setLogLevel(5);
    testing::internal::CaptureStdout();

    const auto debugLine = std::source_location::current().line() + 1;
    opk::log::debug("value {}", 7);
    opk::log::flush();

    const std::string expected = std::string(opk::log::color::BrightCyan) +
                                 "[LoggingTest.cpp:" + std::to_string(debugLine) + "] " +
                                 std::string(opk::log::color::ResetColor) + "value 7\n";
    EXPECT_EQ(testing::internal::GetCapturedStdout(), expected);
}

TEST_F(OpkLogTest, FiltersDebugAtDefaultInfoLevel) {
    testing::internal::CaptureStdout();

    opk::log::debug("filtered");
    opk::log::flush();

    EXPECT_EQ(testing::internal::GetCapturedStdout(), "");
}

TEST_F(OpkLogTest, WritesUnconditionalOutputToOneStream) {
    opk::log::setLogLevel(0);
    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();

    opk::log::instantInfo("stdout {}\n", 1);
    opk::log::instantError("stderr {}\n", 2);

    EXPECT_EQ(testing::internal::GetCapturedStdout(), "stdout 1\n");
    EXPECT_EQ(testing::internal::GetCapturedStderr(), "stderr 2\n");
}

TEST_F(OpkLogTest, EscapesControlCharactersInLogArguments) {
    testing::internal::CaptureStdout();

    opk::log::info("input: {}\n", "camera {} árvíz");
    opk::log::info("input: {}\n", "line\r\n\033\u2028next");
    opk::log::flush();

    EXPECT_EQ(testing::internal::GetCapturedStdout(),
              "input: camera {} árvíz\ninput: line\\r\\n\\x1b\\u2028next\n");
}

TEST_F(OpkLogTest, ClampsLogLevelToSupportedRange) {
    opk::log::setLogLevel(-1);
    EXPECT_EQ(opk::log::getLogLevel(), 0);

    opk::log::setLogLevel(6);
    EXPECT_EQ(opk::log::getLogLevel(), 5);
}
