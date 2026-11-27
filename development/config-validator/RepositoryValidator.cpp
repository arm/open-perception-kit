/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

#include "RepositoryValidatorInternal.h"
#include "ValidatorInternal.h"

#include <algorithm>

namespace opk::config::detail {

std::set<std::filesystem::path> modelArtifactPaths(const std::filesystem::path &root,
                                                   ValidationReport &report) {
    std::set<std::filesystem::path> paths;
    const std::string source = "config/model-assets.json";
    const auto manifest = root / source;
    if (!std::filesystem::exists(manifest))
        return paths;
    const auto content = readText(manifest);
    if (!content) {
        report.issues.push_back(
            makeIssue("json.read", ValidationPhase::Parse, source, "", content.error()));
        return paths;
    }
    const auto parsed = parseJson(*content, source);
    if (!parsed) {
        append(report, parsed.error());
        return paths;
    }
    const auto invalidManifest = [&] {
        report.issues.push_back(
            makeIssue("repository.artifacts",
                      ValidationPhase::Parse,
                      source,
                      "",
                      "artifact manifest requires objects and relative destination paths"));
    };
    const auto add = [&](const std::filesystem::path &relative) {
        if (relative.empty() || relative.is_absolute() ||
            std::ranges::find(relative, std::filesystem::path("..")) != relative.end()) {
            invalidManifest();
            return;
        }
        const auto name = relative.filename().string();
        // A manifest entry must never exempt an OPK descriptor from validation.
        if (name == "model.json" || name == "opchain.json" || name.starts_with("model-") ||
            name.starts_with("opchain-"))
            return;
        paths.insert((root / "config/models" / relative).lexically_normal());
    };
    if (!parsed->is_object()) {
        invalidManifest();
        return paths;
    }
    for (const auto &entry : parsed->object_range()) {
        if (!entry.value().is_object()) {
            invalidManifest();
            continue;
        }
        if (!entry.value().contains("archive")) {
            add(std::filesystem::path(entry.key()));
            continue;
        }
        const auto &archive = entry.value().at("archive");
        if (!archive.is_object()) {
            invalidManifest();
            continue;
        }
        for (const auto &member : archive.object_range())
            add(std::filesystem::path(entry.key()) / std::filesystem::path(member.key()));
    }
    return paths;
}

ValidationReport validateRepository(const std::filesystem::path &root) {
    ValidationReport report;
    auto schemas = loadSchemaBundle(root);
    if (!schemas.has_value())
        return std::move(schemas.error());

    const auto artifacts = modelArtifactPaths(root, report);
    std::vector<std::filesystem::path> descriptors;
    for (const auto &relativeDirectory : {std::filesystem::path("config/models"),
                                          std::filesystem::path("config/opchains"),
                                          std::filesystem::path("config/pipelines")}) {
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
            if (iterator->is_regular_file(error) && iterator->path().extension() == ".json" &&
                !artifacts.contains(iterator->path().lexically_normal()))
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

        const auto type = source.starts_with("config/pipelines/")
                              ? std::optional{DescriptorType::Pipeline}
                              : std::nullopt;
        if (auto result = validateDocument(*content, source, *schemas, type); !result.has_value())
            append(report, std::move(result.error()));
    }

    report.sort();
    return report;
}

} // namespace opk::config::detail
