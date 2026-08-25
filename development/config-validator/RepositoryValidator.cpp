/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "RepositoryValidatorInternal.h"
#include "ValidatorInternal.h"

#include <algorithm>

namespace pek::config::detail {

ValidationReport validateRepository(const std::filesystem::path &root) {
    ValidationReport report;
    auto schemas = loadSchemaBundle(root);
    if (!schemas.has_value())
        return std::move(schemas.error());

    std::vector<std::filesystem::path> descriptors;
    for (const auto &relativeDirectory :
         {std::filesystem::path("config/models"), std::filesystem::path("config/opchains")}) {
        const auto directory = root / relativeDirectory;
        std::error_code error;
        if (!std::filesystem::is_directory(directory, error)) {
            report.issues.push_back(makeIssue("repository.discovery",
                                              ValidationPhase::Parse,
                                              relativeDirectory.generic_string(),
                                              "",
                                              "descriptor directory is missing"));
            continue;
        }

        std::filesystem::recursive_directory_iterator iterator(
            directory, std::filesystem::directory_options::skip_permission_denied, error);
        const std::filesystem::recursive_directory_iterator end;
        while (!error && iterator != end) {
            if (iterator->is_regular_file(error) && iterator->path().extension() == ".json")
                descriptors.push_back(iterator->path());
            iterator.increment(error);
        }
        if (error) {
            report.issues.push_back(makeIssue("repository.discovery",
                                              ValidationPhase::Parse,
                                              relativeDirectory.generic_string(),
                                              "",
                                              error.message()));
        }
    }

    std::ranges::sort(descriptors);
    for (const auto &path : descriptors) {
        const std::string source = relativeSource(path, root);
        auto content = readText(path);
        if (!content.has_value()) {
            report.issues.push_back(makeIssue(
                "json.read", ValidationPhase::Parse, source, "", std::move(content.error())));
            continue;
        }

        if (auto result = validateDocument(*content, source, *schemas); !result.has_value())
            append(report, std::move(result.error()));
    }

    report.sort();
    return report;
}

} // namespace pek::config::detail
