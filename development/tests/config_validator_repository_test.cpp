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

using pek::config::test::hasRule;
using pek::config::test::TemporaryRepository;

TEST(ConfigValidator, RepositoryAllowsSharedNamesForBackendVariants) {
    TemporaryRepository repository;
    repository.writeModel("first", "Shared name");
    repository.writeModel("second", "Shared name");

    const auto report = pek::config::detail::validateRepository(repository.root());

    EXPECT_TRUE(report.ok()) << report.toText();
}

TEST(ConfigValidator, RepositoryMetaValidatesLiveSchemaBundle) {
    TemporaryRepository repository;
    const auto schemaPath = repository.root() / "config/schemas/v1/model.schema.json";
    std::ifstream input(schemaPath);
    auto schema = nlohmann::json::parse(input);
    schema["$id"] = "urn:wrong:model";
    std::ofstream(schemaPath) << schema;

    const auto report = pek::config::detail::validateRepository(repository.root());

    EXPECT_TRUE(hasRule(report, "schema.id"));
}

TEST(ConfigValidator, RepositoryReportsMissingSchemaAndDescriptorDirectory) {
    TemporaryRepository repository;
    std::filesystem::remove(repository.root() / "config/schemas/v1/model.schema.json");
    auto report = pek::config::detail::validateRepository(repository.root());
    EXPECT_TRUE(hasRule(report, "schema.read"));

    const TemporaryRepository missingDirectoryRepository;
    std::filesystem::remove_all(missingDirectoryRepository.root() / "config/opchains");
    report = pek::config::detail::validateRepository(missingDirectoryRepository.root());
    EXPECT_TRUE(hasRule(report, "repository.discovery"));
}

TEST(ConfigValidator, RepositoryRejectsUnknownDescriptorFilename) {
    TemporaryRepository repository;
    std::ofstream(repository.root() / "config/models/first/descriptor.json") << "{}";

    const auto report = pek::config::detail::validateRepository(repository.root());

    EXPECT_TRUE(hasRule(report, "dispatch.filename"));
}

TEST(ConfigValidator, RepositoryRejectsEmptyOpChainVariant) {
    TemporaryRepository repository;
    std::ofstream(repository.root() / "config/opchains/opchain-.json") << "{}";

    const auto report = pek::config::detail::validateRepository(repository.root());

    EXPECT_TRUE(hasRule(report, "dispatch.filename"));
    EXPECT_FALSE(hasRule(report, "dispatch.version"));
}

TEST(ConfigValidator, RepositoryRejectsEmptyModelVariant) {
    TemporaryRepository repository;
    std::ofstream(repository.root() / "config/models/first/model-.json") << "{}";

    const auto report = pek::config::detail::validateRepository(repository.root());

    EXPECT_TRUE(hasRule(report, "dispatch.filename"));
    EXPECT_FALSE(hasRule(report, "dispatch.version"));
}

TEST(ConfigValidator, RepositoryLoadsPaddleRecognitionWithOnnxCompatibleInput) {
    const auto path = std::filesystem::path(PEK_REPOSITORY_ROOT) /
                      "config/models/paddleocr/model-recognition.json";
    std::ifstream input(path);
    std::ostringstream content;
    content << input.rdbuf();

    const auto descriptor = pek::config::validateModelJson(content.str(), path.string());

    ASSERT_TRUE(descriptor) << (descriptor ? "" : descriptor.error().toText());
    ASSERT_EQ(descriptor->inputTensors.size(), 1U);
    EXPECT_EQ(descriptor->inputTensors.front().shape, pek::Shape(1, 3, 48, 640));
}

TEST(ConfigValidator, CheckedInRepositoryIsValidAndCanonicalRoundTrips) {
    const auto report = pek::config::detail::validateRepository(PEK_REPOSITORY_ROOT);

    ASSERT_TRUE(report.ok()) << report.toText();
    for (const auto *directory : {"config/models", "config/opchains"}) {
        for (const auto &entry : std::filesystem::recursive_directory_iterator(
                 std::filesystem::path(PEK_REPOSITORY_ROOT) / directory)) {
            const auto &path = entry.path();
            if (!entry.is_regular_file() || path.extension() != ".json")
                continue;

            std::ifstream input(path);
            std::ostringstream content;
            content << input.rdbuf();
            if (path.filename().string().starts_with("model")) {
                const auto descriptor =
                    pek::config::validateModelJson(content.str(), path.string());
                ASSERT_TRUE(descriptor) << path << '\n' << descriptor.error().toText();
                const auto roundTrip = pek::config::validateModelJson(
                    nlohmann::json(*descriptor).dump(), path.string());
                EXPECT_TRUE(roundTrip) << path << '\n'
                                       << (roundTrip ? "" : roundTrip.error().toText());
            } else {
                const auto descriptor =
                    pek::config::validateOpChainJson(content.str(), path.string());
                ASSERT_TRUE(descriptor) << path << '\n' << descriptor.error().toText();
                const auto roundTrip = pek::config::validateOpChainJson(
                    nlohmann::json(*descriptor).dump(), path.string());
                EXPECT_TRUE(roundTrip) << path << '\n'
                                       << (roundTrip ? "" : roundTrip.error().toText());
            }
        }
    }
}

} // namespace
