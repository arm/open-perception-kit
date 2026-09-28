/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 *************************************************************/

#include <gtest/gtest.h>

#include "Validator.h"

TEST(ConfigValidator, TextReportUsesStableOrder) {
    opk::config::ValidationReport report;
    report.issues.emplace_back(
        "b", opk::config::ValidationPhase::Descriptor, "z.json", "/z", std::nullopt, "second");
    report.issues.emplace_back(
        "a", opk::config::ValidationPhase::Parse, "a.json", "", std::nullopt, "first");
    report.sort();

    EXPECT_EQ(report.toText(),
              "a.json: parse a: first\n"
              "z.json:/z: descriptor b: second\n");
}
