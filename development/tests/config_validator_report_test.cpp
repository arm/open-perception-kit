/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include "Validator.h"

TEST(ConfigValidator, TextReportUsesStableOrder) {
    pek::config::ValidationReport report;
    report.issues.emplace_back(
        "b", pek::config::ValidationPhase::Descriptor, "z.json", "/z", std::nullopt, "second");
    report.issues.emplace_back(
        "a", pek::config::ValidationPhase::Parse, "a.json", "", std::nullopt, "first");
    report.sort();

    EXPECT_EQ(report.toText(),
              "a.json: parse a: first\n"
              "z.json:/z: descriptor b: second\n");
}
