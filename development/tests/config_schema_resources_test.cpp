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
#include <string>
#include <vector>

namespace {

using Json = jsoncons::json;

TEST(ConfigSchemaResources, AreMetaValidWithUniqueIdsAndResolvableReferences) {
    const auto schemaRoot = std::filesystem::path{PEK_REPOSITORY_ROOT} / "config/schemas/v1";
    std::vector<std::filesystem::path> paths;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(schemaRoot)) {
        if (entry.is_regular_file() && entry.path().filename().string().ends_with(".schema.json"))
            paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end());
    ASSERT_FALSE(paths.empty());

    const auto metaSchema = jsoncons::jsonschema::make_json_schema(
        jsoncons::jsonschema::draft202012::schema_draft202012<Json>::get_schema());
    std::map<std::string, Json, std::less<>> schemas;
    std::vector<std::pair<std::filesystem::path, Json>> documents;

    for (const auto &path : paths) {
        std::ifstream input(path);
        ASSERT_TRUE(input) << path;
        const std::string text{std::istreambuf_iterator<char>{input}, {}};
        Json schema;
        ASSERT_NO_THROW(schema = Json::parse(text)) << path;
        ASSERT_TRUE(schema.is_object() && schema.contains("$id") && schema.at("$id").is_string())
            << path;
        EXPECT_TRUE(metaSchema.is_valid(schema)) << path;

        const auto id = schema.at("$id").as<std::string>();
        ASSERT_FALSE(id.empty()) << path;
        ASSERT_TRUE(schemas.emplace(id, schema).second) << "duplicate $id " << id;
        documents.emplace_back(path, std::move(schema));
    }

    const auto resolver = [&schemas](const jsoncons::uri &uri) {
        const auto schema = schemas.find(uri.base().string());
        return schema == schemas.end() ? Json::null() : schema->second;
    };
    for (const auto &[path, schema] : documents) {
        EXPECT_NO_THROW(jsoncons::jsonschema::make_json_schema(schema, resolver)) << path;
    }
}

} // namespace
