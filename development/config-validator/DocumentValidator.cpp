/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 *************************************************************/

#include "Log.h"
#include "ValidatorInternal.h"

#include <jsoncons/json_filter.hpp>
#include <jsoncons/json_reader.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <format>
#include <regex>
#include <unordered_set>

namespace opk::config::detail {
namespace {

const std::regex VersionPattern(R"(^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$)");

class DuplicateKeyFilter final : public jsoncons::json_filter {
  public:
    DuplicateKeyFilter(jsoncons::json_visitor &destination, std::string_view source)
        : jsoncons::json_filter(destination), source_(source) {}

    [[nodiscard]] ValidationReport takeReport() {
        return std::move(report_);
    }

  private:
    JSONCONS_VISITOR_RETURN_TYPE visit_begin_object(jsoncons::semantic_tag tag,
                                                    const jsoncons::ser_context &context,
                                                    std::error_code &error) override {
        keys_.emplace_back();
        destination().begin_object(tag, context, error);
        JSONCONS_VISITOR_RETURN;
    }

    JSONCONS_VISITOR_RETURN_TYPE visit_end_object(const jsoncons::ser_context &context,
                                                  std::error_code &error) override {
        destination().end_object(context, error);
        if (!keys_.empty())
            keys_.pop_back();
        JSONCONS_VISITOR_RETURN;
    }

    JSONCONS_VISITOR_RETURN_TYPE visit_key(const string_view_type &name,
                                           const jsoncons::ser_context &context,
                                           std::error_code &error) override {
        if (const std::string key(name); !keys_.empty() && !keys_.back().insert(key).second) {
            report_.issues.push_back(makeIssue("json.duplicate-key",
                                               ValidationPhase::Parse,
                                               source_,
                                               "",
                                               std::format("duplicate object key '{}' at line {}, "
                                                           "column {}",
                                                           key,
                                                           context.line(),
                                                           context.column())));
        }
        destination().key(name, context, error);
        JSONCONS_VISITOR_RETURN;
    }

    std::string source_;
    std::vector<std::unordered_set<std::string>> keys_;
    ValidationReport report_;
};

bool dispatchVersion(const Json &document,
                     std::string_view source,
                     std::string_view supported,
                     ValidationReport &report) {
    if (!document.is_object()) {
        report.issues.push_back(makeIssue("dispatch.version",
                                          ValidationPhase::Dispatch,
                                          source,
                                          "",
                                          "descriptor must be a JSON object"));
        return false;
    }

    if (!document.contains("version") || !document.at("version").is_string()) {
        report.issues.push_back(
            makeIssue("dispatch.version",
                      ValidationPhase::Dispatch,
                      source,
                      "/version",
                      std::format("version must be a MAJOR.MINOR.PATCH string; supported "
                                  "version: {}. Migrate the configuration before running.",
                                  supported)));
        return false;
    }

    const std::string version = document.at("version").as<std::string>();
    if (!std::regex_match(version, VersionPattern)) {
        report.issues.push_back(
            makeIssue("dispatch.version",
                      ValidationPhase::Dispatch,
                      source,
                      "/version",
                      std::format("version must be a MAJOR.MINOR.PATCH string; supported "
                                  "version: {}. Migrate the configuration before running.",
                                  supported)));
        return false;
    }

    const std::string_view descriptorVersion{version};
    const auto majorEnd = descriptorVersion.find('.');
    const auto minorEnd = descriptorVersion.find('.', majorEnd + 1);

    const auto minorStart = supported.find('.') + 1;
    if (descriptorVersion.substr(0, majorEnd) != supported.substr(0, minorStart - 1)) {
        report.issues.push_back(
            makeIssue("dispatch.unsupported",
                      ValidationPhase::Dispatch,
                      source,
                      "/version",
                      std::format("incompatible descriptor major version in {}; "
                                  "supported version: {}. Use a matching OPK "
                                  "release or migrate the configuration.",
                                  version,
                                  supported)));
        return false;
    }
    if (descriptorVersion.substr(majorEnd + 1, minorEnd - majorEnd - 1) !=
        supported.substr(minorStart, supported.find('.', minorStart) - minorStart)) {
        opk::log::warning("{}:/version: warning: descriptor minor version differs: {}; supported "
                          "version: {}. Continuing with the supported schema.\n",
                          source,
                          version,
                          supported);
    }
    return true;
}

std::optional<DescriptorType> descriptorTypeFromFilename(const std::filesystem::path &path,
                                                         std::string_view source,
                                                         ValidationReport &report) {
    const std::string filename = path.filename().string();
    constexpr std::string_view ModelPrefix = "model-";
    constexpr std::string_view OpChainPrefix = "opchain-";
    constexpr std::string_view JsonSuffix = ".json";
    if (filename == "model.json" ||
        (filename.starts_with(ModelPrefix) && filename.ends_with(JsonSuffix) &&
         filename.size() > ModelPrefix.size() + JsonSuffix.size()))
        return DescriptorType::Model;
    if (filename == "opchain.json" ||
        (filename.starts_with(OpChainPrefix) && filename.ends_with(JsonSuffix) &&
         filename.size() > OpChainPrefix.size() + JsonSuffix.size()))
        return DescriptorType::OpChain;

    report.issues.push_back(
        makeIssue("dispatch.filename",
                  ValidationPhase::Dispatch,
                  source,
                  "",
                  "descriptor filename must be model.json, model-<variant>.json, opchain.json, or "
                  "opchain-<variant>.json"));
    return std::nullopt;
}

ValidationReport
validateAgainstSchema(const Json &document, const CompiledSchema &schema, std::string_view source) {
    ValidationReport report;
    schema.validate(document,
                    [&report, source](const jsoncons::jsonschema::validation_message &message) {
                        report.issues.push_back(makeIssue("schema.validation",
                                                          ValidationPhase::Schema,
                                                          source,
                                                          message.instance_location().string(),
                                                          message.message()));
                        return jsoncons::jsonschema::walk_state::advance;
                    });
    report.sort();
    return report;
}

} // namespace

JsonResult parseJson(std::string_view text, std::string_view source) {
    jsoncons::json_decoder<Json> decoder;
    DuplicateKeyFilter filter(decoder, source);

    try {
        jsoncons::json_string_reader reader(text, filter);
        reader.read();
    } catch (const jsoncons::ser_error &error) {
        ValidationReport report = filter.takeReport();
        report.issues.push_back(makeIssue(
            "json.syntax",
            ValidationPhase::Parse,
            source,
            "",
            std::format("{} at line {}, column {}", error.what(), error.line(), error.column())));
        report.sort();
        return tl::unexpected{std::move(report)};
    }

    if (ValidationReport duplicates = filter.takeReport(); !duplicates.ok()) {
        duplicates.sort();
        return tl::unexpected{std::move(duplicates)};
    }
    return decoder.get_result();
}

DocumentResult validateDocument(std::string_view json,
                                std::string_view source,
                                const SchemaBundle &schemas,
                                std::optional<DescriptorType> expectedType) {
    auto parsed = parseJson(json, source);
    if (!parsed.has_value())
        return tl::unexpected{std::move(parsed.error())};

    ValidationReport report;
    const auto type =
        expectedType == DescriptorType::Pipeline
            ? expectedType
            : descriptorTypeFromFilename(std::filesystem::path(source), source, report);
    if (!type.has_value())
        return tl::unexpected{std::move(report)};
    if (expectedType.has_value() && *type != *expectedType) {
        report.issues.push_back(makeIssue("dispatch.filename",
                                          ValidationPhase::Dispatch,
                                          source,
                                          "",
                                          *expectedType == DescriptorType::Model
                                              ? "Model validation requires a model.json or "
                                                "model-<variant>.json filename"
                                              : "OpChain validation requires an opchain.json or "
                                                "opchain-<variant>.json filename"));
        return tl::unexpected{std::move(report)};
    }
    const CompiledSchema *schema = &schemas.opchain;
    std::string_view supported = schemas.opchainVersion;
    if (*type == DescriptorType::Pipeline) {
        schema = &schemas.pipeline;
        supported = schemas.pipelineVersion;
    } else if (*type == DescriptorType::Model) {
        schema = &schemas.model;
        supported = schemas.modelVersion;
    }
    if (!dispatchVersion(*parsed, source, supported, report)) {
        report.sort();
        return tl::unexpected{std::move(report)};
    }
    report = validateAgainstSchema(*parsed, *schema, source);
    if (!report.ok())
        return tl::unexpected{std::move(report)};

    try {
        nlohmann::json projection = nlohmann::json::parse(json);
        if (*type == DescriptorType::Pipeline) {
            // jsoncons pattern validation uses C strings; check the complete command too.
            // items() uses array-position strings as keys, or an empty key for a scalar string.
            for (const auto &[index, command] : projection.at("pipeline").items()) {
                if (command.get_ref<const std::string &>().find('\0') != std::string::npos)
                    report.issues.push_back(
                        makeIssue("pipeline.v1.command",
                                  ValidationPhase::Descriptor,
                                  source,
                                  index.empty() ? "/pipeline" : std::format("/pipeline/{}", index),
                                  "pipeline command must not contain NUL bytes"));
            }
            if (!report.ok())
                return tl::unexpected{std::move(report)};
            return Descriptor{std::move(projection)};
        }
        if (*type == DescriptorType::Model) {
            auto descriptor = projection.get<opk::ModelDescriptor>();
            report = validateModelV1(descriptor, source);
            if (!report.ok())
                return tl::unexpected{std::move(report)};
            return Descriptor{std::move(descriptor)};
        }

        auto descriptor = projection.get<opk::op::OpChainDescriptor>();
        report = validateOpChainV1(descriptor, source);
        if (!report.ok())
            return tl::unexpected{std::move(report)};
        return Descriptor{std::move(descriptor)};
    } catch (const nlohmann::json::exception &error) {
        report.issues.push_back(
            makeIssue("projection.typed", ValidationPhase::Descriptor, source, "", error.what()));
        return tl::unexpected{std::move(report)};
    }
}

} // namespace opk::config::detail
