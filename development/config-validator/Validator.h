/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "op/OpChainDescriptor.h"
#include "pek/ModelDescriptor.h"

#include <tl/expected.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pek::config {

enum class ValidationPhase { Parse, Dispatch, Schema, Descriptor };

struct ValidationIssue {
    std::string rule;
    ValidationPhase phase;
    std::string file;
    std::string instanceLocation;
    std::optional<std::string> relatedInstanceLocation;
    std::string message;
};

struct ValidationReport {
    std::vector<ValidationIssue> issues;

    [[nodiscard]] bool ok() const noexcept {
        return issues.empty();
    }

    void sort();
    [[nodiscard]] std::string toText() const;
};

[[nodiscard]] tl::expected<pek::ModelDescriptor, ValidationReport>
validateModelJson(std::string_view json, std::string_view source = "model.json");

[[nodiscard]] tl::expected<pek::op::OpChainDescriptor, ValidationReport>
validateOpChainJson(std::string_view json, std::string_view source = "opchain.json");

[[nodiscard]] ValidationReport
validateOpChainSemantics(const pek::op::OpChainDescriptor &descriptor,
                         std::string_view source = "<typed>");

} // namespace pek::config
