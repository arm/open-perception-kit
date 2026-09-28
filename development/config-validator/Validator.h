/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

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
