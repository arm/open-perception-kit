/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "RepositoryValidatorInternal.h"
#include "config_validator_test_support.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

using opk::config::test::hasRule;
using opk::config::test::TemporaryRepository;

TEST(ConfigValidator, RepositoryAllowsSharedNamesForBackendVariants) {
    TemporaryRepository repository;
    repository.writeModel("first", "Shared name");
    repository.writeModel("second", "Shared name");

    const auto report = opk::config::detail::validateRepository(repository.root());

    EXPECT_TRUE(report.ok()) << report.toText();
}

TEST(ConfigValidator, RepositoryMetaValidatesLiveSchemaBundle) {
    TemporaryRepository repository;
    const auto schemaPath = repository.root() / "config/schemas/v1/model.schema.json";
    std::ifstream input(schemaPath);
    auto schema = nlohmann::json::parse(input);
    schema["$id"] = "urn:wrong:model";
    std::ofstream(schemaPath) << schema;

    const auto report = opk::config::detail::validateRepository(repository.root());

    EXPECT_TRUE(hasRule(report, "schema.id"));
}

TEST(ConfigValidator, RepositoryReportsMissingSchemaAndDescriptorDirectory) {
    TemporaryRepository repository;
    std::filesystem::remove(repository.root() / "config/schemas/v1/model.schema.json");
    auto report = opk::config::detail::validateRepository(repository.root());
    EXPECT_TRUE(hasRule(report, "schema.read"));

    const TemporaryRepository missingDirectoryRepository;
    std::filesystem::remove_all(missingDirectoryRepository.root() / "config/opchains");
    report = opk::config::detail::validateRepository(missingDirectoryRepository.root());
    EXPECT_TRUE(hasRule(report, "repository.discovery"));
}

TEST(ConfigValidator, RepositoryRejectsUnknownDescriptorFilename) {
    TemporaryRepository repository;
    std::ofstream(repository.root() / "config/models/first/descriptor.json") << "{}";

    const auto report = opk::config::detail::validateRepository(repository.root());

    EXPECT_TRUE(hasRule(report, "dispatch.filename"));
}

TEST(ConfigValidator, RepositoryRejectsEmptyOpChainVariant) {
    TemporaryRepository repository;
    std::ofstream(repository.root() / "config/opchains/opchain-.json") << "{}";

    const auto report = opk::config::detail::validateRepository(repository.root());

    EXPECT_TRUE(hasRule(report, "dispatch.filename"));
    EXPECT_FALSE(hasRule(report, "dispatch.version"));
}

TEST(ConfigValidator, RepositoryValidatesNestedPipelinesByDirectory) {
    TemporaryRepository repository;
    const auto directory = repository.root() / "config/pipelines/testing";
    std::filesystem::create_directory(directory);
    const auto path = directory / "model.json";
    std::ofstream(path) << R"({"version":"2.0.0","description":"future","pipeline":"fakesrc"})";
    const auto unsupported = opk::config::detail::validateRepository(repository.root());
    ASSERT_EQ(unsupported.issues.size(), 1U);
    EXPECT_TRUE(hasRule(unsupported, "dispatch.unsupported"));
    EXPECT_EQ(unsupported.issues.front().file, "config/pipelines/testing/model.json");
    std::ofstream(path) << R"({"version":"1.0.0","description":"valid","pipeline":"fakesrc"})";
    EXPECT_TRUE(opk::config::detail::validateRepository(repository.root()).ok());
}

TEST(ConfigValidator, RepositoryRejectsEmptyModelVariant) {
    TemporaryRepository repository;
    std::ofstream(repository.root() / "config/models/first/model-.json") << "{}";

    const auto report = opk::config::detail::validateRepository(repository.root());

    EXPECT_TRUE(hasRule(report, "dispatch.filename"));
    EXPECT_FALSE(hasRule(report, "dispatch.version"));
}

TEST(ConfigValidator, RepositoryLoadsMobileGazeWithStaticOutputs) {
    const auto path = std::filesystem::path(OPK_REPOSITORY_ROOT) /
                      "config/models/mobilegaze-mobilenet-v2/model.json";
    std::ifstream input(path);
    std::ostringstream content;
    content << input.rdbuf();

    const auto descriptor = opk::config::validateModelJson(content.str(), path.string());

    ASSERT_TRUE(descriptor) << (descriptor ? "" : descriptor.error().toText());
    ASSERT_EQ(descriptor->inputTensors.size(), 1U);
    EXPECT_EQ(descriptor->inputTensors.front().shape, opk::Shape(1, 3, 448, 448));
    ASSERT_EQ(descriptor->outputTensors.size(), 2U);
    EXPECT_EQ(descriptor->outputTensors.front().shape, opk::Shape(1, 90));
}

TEST(ConfigValidator, CheckedInRepositoryIsValidAndCanonicalRoundTrips) {
    const auto report = opk::config::detail::validateRepository(OPK_REPOSITORY_ROOT);

    ASSERT_TRUE(report.ok()) << report.toText();
    for (const auto *directory :
         {"config/models", "config/opchains", "development/examples/byom-blazeface"}) {
        for (const auto &entry : std::filesystem::recursive_directory_iterator(
                 std::filesystem::path(OPK_REPOSITORY_ROOT) / directory)) {
            const auto &path = entry.path();
            if (!entry.is_regular_file() || path.extension() != ".json")
                continue;

            std::ifstream input(path);
            std::ostringstream content;
            content << input.rdbuf();
            if (path.filename() == "pipeline.json") {
                const auto pipeline =
                    opk::config::validatePipelineJson(content.str(), path.string());
                ASSERT_TRUE(pipeline) << path << '\n' << pipeline.error().toText();
                continue;
            }
            if (path.filename().string().starts_with("model")) {
                const auto descriptor =
                    opk::config::validateModelJson(content.str(), path.string());
                ASSERT_TRUE(descriptor) << path << '\n' << descriptor.error().toText();
                const auto roundTrip = opk::config::validateModelJson(
                    nlohmann::json(*descriptor).dump(), path.string());
                EXPECT_TRUE(roundTrip) << path << '\n'
                                       << (roundTrip ? "" : roundTrip.error().toText());
            } else {
                const auto descriptor =
                    opk::config::validateOpChainJson(content.str(), path.string());
                ASSERT_TRUE(descriptor) << path << '\n' << descriptor.error().toText();
                const auto roundTrip = opk::config::validateOpChainJson(
                    nlohmann::json(*descriptor).dump(), path.string());
                EXPECT_TRUE(roundTrip) << path << '\n'
                                       << (roundTrip ? "" : roundTrip.error().toText());
            }
        }
    }
}

} // namespace
