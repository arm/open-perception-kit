/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 *************************************************************/

#pragma once

#include "op/OpChainDescriptor.h"
#include "opk/ModelDescriptor.h"

#include <nlohmann/json.hpp>
#include <tl/expected.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opk::config {

enum class ValidationPhase { Parse, Dispatch, Schema, Descriptor };

struct ValidationIssue {
    std::string rule;
    ValidationPhase phase;
    std::string file;
    std::string instanceLocation;
    std::optional<std::string> relatedInstanceLocation;
    std::string message;

    [[nodiscard]] std::string toText() const;
};

struct ValidationReport {
    std::vector<ValidationIssue> issues;

    [[nodiscard]] bool ok() const noexcept {
        return issues.empty();
    }

    void sort();
    [[nodiscard]] std::string toText() const;
};

[[nodiscard]] tl::expected<opk::ModelDescriptor, ValidationReport>
validateModelJson(std::string_view json, std::string_view source = "model.json");

[[nodiscard]] tl::expected<nlohmann::json, ValidationReport>
validatePipelineJson(std::string_view json, std::string_view source = "pipeline.json");

[[nodiscard]] tl::expected<opk::op::OpChainDescriptor, ValidationReport>
validateOpChainJson(std::string_view json, std::string_view source = "opchain.json");

[[nodiscard]] tl::expected<std::string, ValidationReport> supportedPipelineVersion();

[[nodiscard]] tl::expected<std::string, ValidationReport> supportedModelVersion();

[[nodiscard]] tl::expected<std::string, ValidationReport> supportedOpChainVersion();

[[nodiscard]] ValidationReport
validateOpChainSemantics(const opk::op::OpChainDescriptor &descriptor,
                         std::string_view source = "<typed>");

} // namespace opk::config
