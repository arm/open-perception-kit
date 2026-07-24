/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "Log.h"
#include "tools.h"

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
        ASSERT_TRUE(pek::log::setLogTargetState(pek::log::TargetType::Stdout, true));
        ASSERT_TRUE(pek::log::setLogTargetState(pek::log::TargetType::Stderr, false));
        ASSERT_TRUE(pek::log::setLogTargetState(pek::log::TargetType::File, false));
        pek::log::flush();
    }

    void TearDown() override {
        pek::log::flush();
    }
};

} // namespace

TEST(PekLog, OrdersLevelsByVerbosityAndDefaultsToInfo) {
    EXPECT_EQ(static_cast<int>(pek::log::Level::Off), 0);
    EXPECT_EQ(static_cast<int>(pek::log::Level::Error), 1);
    EXPECT_EQ(static_cast<int>(pek::log::Level::Warn), 2);
    EXPECT_EQ(static_cast<int>(pek::log::Level::Notice), 3);
    EXPECT_EQ(static_cast<int>(pek::log::Level::Info), 4);
    EXPECT_EQ(pek::log::defaultLogLevel, pek::log::Level::Info);
}

TEST(tools, InvertsText) {
    EXPECT_EQ(pek::log::tools::invert("notice\n"), "\033[7mnotice\n\033[0m");
}

TEST_F(PekLogTest, AppliesSeverityThresholdAtEveryConfiguredLevel) {
    struct ExpectedOutput {
        pek::log::Level configuredLevel;
        std::string output;
    };

    const std::array expectations{
        ExpectedOutput{pek::log::Level::Off, ""},
        ExpectedOutput{pek::log::Level::Error, "E: error\n"},
        ExpectedOutput{pek::log::Level::Warn, "W: warning\nE: error\n"},
        ExpectedOutput{pek::log::Level::Notice,
                       pek::log::tools::invert("notice\n") + "W: warning\nE: error\n"},
        ExpectedOutput{pek::log::Level::Info,
                       "info\n" + pek::log::tools::invert("notice\n") + "W: warning\nE: error\n"},
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
        pek::log::flush();

        EXPECT_EQ(testing::internal::GetCapturedStdout(), expectation.output);
    }
}

TEST_F(PekLogTest, WritesEverySeverityToEveryEnabledTarget) {
    ASSERT_TRUE(pek::log::setLogTargetState(pek::log::TargetType::Stderr, true));
    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();

    pek::log::info("info\n");
    pek::log::notice("notice\n");
    pek::log::warning("warn\n");
    pek::log::error("error\n");
    pek::log::flush();

    const std::string expected =
        "info\n" + pek::log::tools::invert("notice\n") + "W: warn\nE: error\n";
    EXPECT_EQ(testing::internal::GetCapturedStdout(), expected);
    EXPECT_EQ(testing::internal::GetCapturedStderr(), expected);
}

TEST_F(PekLogTest, DisablesAndEnablesAvailableTargets) {
    EXPECT_EQ(pek::log::getEnabledLogTargets(), std::vector{pek::log::TargetType::Stdout});
    EXPECT_TRUE(pek::log::setLogTargetState(pek::log::TargetType::Stdout, false));
    EXPECT_TRUE(pek::log::getEnabledLogTargets().empty());
    EXPECT_TRUE(pek::log::setLogTargetState(pek::log::TargetType::Stderr, true));
    EXPECT_EQ(pek::log::getEnabledLogTargets(), std::vector{pek::log::TargetType::Stderr});
}

TEST_F(PekLogTest, FiltersMessagesAtConfiguredLevel) {
    pek::log::setLogLevel(2);
    testing::internal::CaptureStdout();

    pek::log::info("info\n");
    pek::log::notice("notice\n");
    pek::log::warning("warn\n");
    pek::log::error("error\n");
    pek::log::flush();

    EXPECT_EQ(testing::internal::GetCapturedStdout(), "W: warn\nE: error\n");
}

TEST_F(PekLogTest, WritesRuntimeFormatsAsynchronously) {
    testing::internal::CaptureStdout();

    pek::log::infoRuntime("runtime {}\n", 1);
    pek::log::warningRuntime("runtime {}\n", 2);
    pek::log::errorRuntime("runtime {}\n", 3);
    pek::log::flush();

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
