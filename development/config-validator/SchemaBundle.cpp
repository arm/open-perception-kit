/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "ValidatorInternal.h"

#include "EmbeddedSchemas.h"
#include "pek/File.h"

#include <jsoncons_ext/jsonschema/draft202012/schema_draft202012.hpp>

#include <format>
#include <map>

namespace pek::config::detail {

tl::expected<std::string, std::string> readText(const std::filesystem::path &path) {
    auto content = pek::fs::loadText(path.string());
    if (!content.has_value())
        return tl::unexpected{std::move(content.error().info)};
    return std::move(*content);
}

std::string relativeSource(const std::filesystem::path &path, const std::filesystem::path &root) {
    const auto relative = path.lexically_relative(root);
    return relative.empty() ? path.generic_string() : relative.generic_string();
}

namespace {

constexpr std::string_view ModelSchemaId = "urn:arm:pek:schema:model-descriptor:v1";
constexpr std::string_view OpChainSchemaId = "urn:arm:pek:schema:opchain-descriptor:v1";
const CompiledSchema Draft202012MetaSchema = jsoncons::jsonschema::make_json_schema(
    jsoncons::jsonschema::draft202012::schema_draft202012<Json>::get_schema());

struct SchemaDocument {
    std::string id;
    std::string text;
    std::string source;
};

void validateSchemaDocument(ValidationReport &report,
                            const Json &schema,
                            std::string_view source,
                            std::string_view expectedId) {
    if (!schema.is_object() || !schema.contains("$id") || !schema.at("$id").is_string() ||
        schema.at("$id").as<std::string>() != expectedId) {
        report.issues.push_back(makeIssue("schema.id",
                                          ValidationPhase::Schema,
                                          source,
                                          "/$id",
                                          std::format("schema $id must be '{}'", expectedId)));
    }

    Draft202012MetaSchema.validate(
        schema, [&report, source](const jsoncons::jsonschema::validation_message &message) {
            report.issues.push_back(makeIssue("schema.meta",
                                              ValidationPhase::Schema,
                                              source,
                                              message.instance_location().string(),
                                              message.message()));
            return jsoncons::jsonschema::walk_state::advance;
        });
}

SchemaBundleResult compileSchemaBundle(const std::vector<SchemaDocument> &documents) {
    ValidationReport report;
    std::map<std::string, Json, std::less<>> parsedSchemas;
    for (const auto &document : documents) {
        auto parsed = parseJson(document.text, document.source);
        if (!parsed.has_value()) {
            append(report, std::move(parsed.error()));
            continue;
        }
        validateSchemaDocument(report, *parsed, document.source, document.id);
        parsedSchemas.try_emplace(document.id, std::move(*parsed));
    }
    if (!report.ok()) {
        report.sort();
        return tl::unexpected{std::move(report)};
    }

    try {
        auto resolver = [&parsedSchemas](const jsoncons::uri &uri) {
            const auto schema = parsedSchemas.find(uri.base().string());
            return schema == parsedSchemas.end() ? Json::null() : schema->second;
        };

        return SchemaBundle{
            jsoncons::jsonschema::make_json_schema(parsedSchemas.at(std::string(ModelSchemaId)),
                                                   resolver),
            jsoncons::jsonschema::make_json_schema(parsedSchemas.at(std::string(OpChainSchemaId)),
                                                   resolver),
        };
    } catch (const jsoncons::jsonschema::schema_error &error) {
        report.issues.push_back(
            makeIssue("schema.compile", ValidationPhase::Schema, "<schema>", "", error.what()));
        return tl::unexpected{std::move(report)};
    }
} // NOSONAR: json_schema owns and releases the factory's unique_ptr.

const SchemaBundleResult EmbeddedSchemaBundle = [] {
    std::vector<SchemaDocument> documents;
    documents.reserve(EmbeddedSchemas.size());
    for (const auto &schema : EmbeddedSchemas)
        documents.emplace_back(
            std::string(schema.id), std::string(schema.text), std::string(schema.path));

    return compileSchemaBundle(documents);
}();

} // namespace

const SchemaBundleResult &embeddedSchemas() {
    return EmbeddedSchemaBundle;
}

SchemaBundleResult loadSchemaBundle(const std::filesystem::path &root) {
    ValidationReport report;
    std::vector<SchemaDocument> documents;
    documents.reserve(EmbeddedSchemas.size());
    for (const auto &schema : EmbeddedSchemas) {
        const auto path = root / schema.path;
        auto text = readText(path);
        if (!text.has_value()) {
            report.issues.push_back(makeIssue("schema.read",
                                              ValidationPhase::Schema,
                                              relativeSource(path, root),
                                              "",
                                              std::move(text.error())));
            continue;
        }
        documents.emplace_back(
            std::string(schema.id), std::move(*text), relativeSource(path, root));
    }
    if (!report.ok())
        return tl::unexpected{std::move(report)};
    return compileSchemaBundle(documents);
}

} // namespace pek::config::detail
