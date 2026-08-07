/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "ValidatorInternal.h"

#include <jsoncons/json_filter.hpp>
#include <jsoncons/json_reader.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <format>
#include <unordered_set>

namespace pek::config::detail {
namespace {

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

bool dispatchVersion(const Json &document, std::string_view source, ValidationReport &report) {
    if (!document.is_object()) {
        report.issues.push_back(makeIssue("dispatch.version",
                                          ValidationPhase::Dispatch,
                                          source,
                                          "",
                                          "descriptor must be a JSON object"));
        return false;
    }

    if (!document.contains("version") ||
        (!document.at("version").is_int64() && !document.at("version").is_uint64())) {
        report.issues.push_back(makeIssue("dispatch.version",
                                          ValidationPhase::Dispatch,
                                          source,
                                          "/version",
                                          "version integer is required"));
        return false;
    }

    const auto &versionValue = document.at("version");
    if (versionValue.is_int64() ? versionValue.as<std::int64_t>() == 1
                                : versionValue.as<std::uint64_t>() == 1)
        return true;

    const std::string version = versionValue.is_int64()
                                    ? std::to_string(versionValue.as<std::int64_t>())
                                    : std::to_string(versionValue.as<std::uint64_t>());
    report.issues.push_back(makeIssue("dispatch.unsupported",
                                      ValidationPhase::Dispatch,
                                      source,
                                      "/version",
                                      std::format("unsupported descriptor version {}", version)));
    return false;
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
    const auto type = descriptorTypeFromFilename(std::filesystem::path(source), source, report);
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
    if (!dispatchVersion(*parsed, source, report)) {
        report.sort();
        return tl::unexpected{std::move(report)};
    }

    const CompiledSchema &schema = *type == DescriptorType::Model ? schemas.model : schemas.opchain;
    report = validateAgainstSchema(*parsed, schema, source);
    if (!report.ok())
        return tl::unexpected{std::move(report)};

    try {
        nlohmann::json projection = nlohmann::json::parse(json);
        if (*type == DescriptorType::Model) {
            auto descriptor = projection.get<pek::ModelDescriptor>();
            report = validateModelV1(descriptor, source);
            if (!report.ok())
                return tl::unexpected{std::move(report)};
            return Descriptor{std::move(descriptor)};
        }

        auto descriptor = projection.get<pek::op::OpChainDescriptor>();
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

} // namespace pek::config::detail
