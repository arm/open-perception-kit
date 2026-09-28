/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 *************************************************************/

#pragma once

#include "Validator.h"

#include <jsoncons/json.hpp>
#include <jsoncons_ext/jsonschema/jsonschema.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace opk::config::detail {

using Json = jsoncons::ojson;
using CompiledSchema = jsoncons::jsonschema::json_schema<Json>;
using Descriptor = std::variant<opk::ModelDescriptor, opk::op::OpChainDescriptor, nlohmann::json>;

enum class DescriptorType { Model, OpChain, Pipeline };

struct SchemaBundle {
    CompiledSchema model;
    CompiledSchema opchain;
    CompiledSchema pipeline;
    std::string modelVersion;
    std::string opchainVersion;
    std::string pipelineVersion;
};

using DocumentResult = tl::expected<Descriptor, ValidationReport>;
using SchemaBundleResult = tl::expected<SchemaBundle, ValidationReport>;
using JsonResult = tl::expected<Json, ValidationReport>;

[[nodiscard]] JsonResult parseJson(std::string_view text, std::string_view source);

[[nodiscard]] tl::expected<std::string, std::string> readText(const std::filesystem::path &path);

[[nodiscard]] std::string relativeSource(const std::filesystem::path &path,
                                         const std::filesystem::path &root);

[[nodiscard]] const SchemaBundleResult &embeddedSchemas();

[[nodiscard]] SchemaBundleResult loadSchemaBundle(const std::filesystem::path &root);

[[nodiscard]] DocumentResult
validateDocument(std::string_view json,
                 std::string_view source,
                 const SchemaBundle &schemas,
                 std::optional<DescriptorType> expectedType = std::nullopt);

[[nodiscard]] ValidationIssue makeIssue(std::string rule,
                                        ValidationPhase phase,
                                        std::string_view file,
                                        std::string instanceLocation,
                                        std::string message,
                                        std::optional<std::string> related = std::nullopt);

void append(ValidationReport &target, ValidationReport source);

void validateControlFreeString(ValidationReport &report,
                               std::string_view value,
                               std::string_view source,
                               std::string_view instanceLocation,
                               std::string_view rule);

[[nodiscard]] ValidationReport validateModelV1(const opk::ModelDescriptor &descriptor,
                                               std::string_view source);

[[nodiscard]] ValidationReport validateOpChainV1(const opk::op::OpChainDescriptor &descriptor,
                                                 std::string_view source);

} // namespace opk::config::detail
