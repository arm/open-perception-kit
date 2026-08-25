/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include <gtest/gtest.h>

#include <jsoncons/json.hpp>
#include <jsoncons_ext/jsonschema/draft202012/schema_draft202012.hpp>
#include <jsoncons_ext/jsonschema/jsonschema.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Json = jsoncons::json;

struct SchemaError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

Json loadSchema(const std::filesystem::path &path) {
    std::ifstream input(path);
    if (!input)
        throw SchemaError("Cannot read schema: " + path.string());

    const std::string text{std::istreambuf_iterator<char>{input}, {}};
    auto schema = Json::parse(text);
    if (!schema.is_object() || !schema.contains("$id") || !schema.at("$id").is_string())
        throw SchemaError("Schema has no string $id: " + path.string());
    return schema;
}

TEST(ConfigSchemaResources, AreMetaValidWithUniqueIdsAndResolvableReferences) {
    const auto schemaRoot = std::filesystem::path{PEK_REPOSITORY_ROOT} / "config/schemas/v1";
    std::vector<std::filesystem::path> paths;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(schemaRoot)) {
        if (entry.is_regular_file() && entry.path().filename().string().ends_with(".schema.json"))
            paths.push_back(entry.path());
    }
    std::ranges::sort(paths);
    ASSERT_FALSE(paths.empty());

    const auto metaSchema = jsoncons::jsonschema::make_json_schema(
        jsoncons::jsonschema::draft202012::schema_draft202012<Json>::get_schema());
    std::map<std::string, Json, std::less<>> schemas;
    std::vector<std::pair<std::filesystem::path, Json>> documents;

    for (const auto &path : paths) {
        auto schema = loadSchema(path);
        if (!metaSchema.is_valid(schema))
            throw SchemaError("Schema is not meta-valid: " + path.string());

        const auto id = schema.at("$id").as<std::string>();
        if (id.empty())
            throw SchemaError("Schema has an empty $id: " + path.string());
        if (!schemas.try_emplace(id, schema).second)
            throw SchemaError("Duplicate schema $id: " + id);
        documents.emplace_back(path, std::move(schema));
    }

    const auto resolver = [&schemas](const jsoncons::uri &uri) {
        const auto schema = schemas.find(uri.base().string());
        return schema == schemas.end() ? Json::null() : schema->second;
    };
    for (const auto &[path, schema] : documents) {
        SCOPED_TRACE(path.string());
        jsoncons::jsonschema::make_json_schema(schema, resolver);
    }
}

} // namespace
