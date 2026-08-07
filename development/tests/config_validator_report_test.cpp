/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "Validator.h"

TEST(ConfigValidator, JsonReportUsesStableEnvelope) {
    pek::config::ValidationReport report;
    report.issues.push_back(pek::config::ValidationIssue{
        "b", pek::config::ValidationPhase::Descriptor, "z.json", "/z", std::nullopt, "second"});
    report.issues.push_back(pek::config::ValidationIssue{
        "a", pek::config::ValidationPhase::Parse, "a.json", "", std::nullopt, "first"});
    report.sort();

    const auto json = nlohmann::json::parse(report.toJson());
    ASSERT_EQ(json.at("diagnosticFormatVersion"), 1);
    ASSERT_EQ(json.at("issues").size(), 2);
    EXPECT_EQ(json.at("issues").at(0).at("rule"), "a");
}
