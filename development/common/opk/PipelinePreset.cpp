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

#include "opk/PipelinePreset.h"
#include "Validator.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <fmt/core.h>
#include <nlohmann/json.hpp>
#include <tl/expected.hpp>

namespace opk {
namespace {

bool isVariableStart(char character) {
    return std::isalpha(static_cast<unsigned char>(character)) || character == '_';
}

bool isVariableCharacter(char character) {
    return std::isalnum(static_cast<unsigned char>(character)) || character == '_';
}

Error invalidPreset(const std::string &source, const std::string &reason) {
    return OPK_ERROR(ErrorFlag::InvalidData, fmt::format("Invalid JSON ({}): {}", reason, source));
}

} // namespace

Result<PipelinePreset> PipelinePreset::fromJson(const std::string &jsonText,
                                                const std::string &source) {
    try {
        const auto document = opk::config::validatePipelineJson(jsonText, source);
        if (!document) {
            const auto flag =
                document.error().issues.front().phase == opk::config::ValidationPhase::Parse
                    ? ErrorFlag::ParseError
                    : ErrorFlag::InvalidData;
            return tl::unexpected(OPK_ERROR(flag, document.error().toText()));
        }

        PipelinePreset preset;

        preset.description = document->at("description").get<std::string>();

        if (document->contains("loop")) {
            preset.loop = document->at("loop").get<bool>();
        }

        if (document->contains("sourceInfo")) {
            if (!document->at("sourceInfo").is_string()) {
                return tl::unexpected(invalidPreset(source, "'sourceInfo' must be a string"));
            }
            preset.sourceInfo = document->at("sourceInfo").get<std::string>();
        }

        const auto &pipeline = document->at("pipeline");
        if (pipeline.is_string()) {
            preset.pipeline = pipeline.get<std::string>();
            return preset;
        }

        bool first = true;
        for (const auto &fragmentValue : pipeline) {
            const auto fragment = fragmentValue.get<std::string>();
            if (fragment.empty()) {
                continue;
            }

            if (!first) {
                preset.pipeline.push_back(' ');
            }
            preset.pipeline += fragment;
            first = false;
        }

        return preset;
    } catch (const nlohmann::json::exception &error) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::ParseError,
                      fmt::format("JSON parse error in {}: {}", source, error.what())));
    }
}

Result<PipelinePreset> PipelinePreset::fromFile(const std::string &path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::FileNotFound, fmt::format("Failed to open: {}", path)));
    }

    std::ostringstream contents;
    contents << input.rdbuf();
    if (input.bad()) {
        return tl::unexpected(
            OPK_ERROR(ErrorFlag::FileOperationError, fmt::format("Failed to read: {}", path)));
    }

    return fromJson(contents.str(), path);
}

Result<std::string> expandPipelineDescription(const std::string &description) {
    std::string expanded;
    expanded.reserve(description.size());

    for (size_t index = 0; index < description.size();) {
        if (description[index] != '$' || index + 1 >= description.size() ||
            description[index + 1] != '{') {
            expanded.push_back(description[index]);
            ++index;
            continue;
        }

        const size_t nameStart = index + 2;
        if (nameStart >= description.size() || !isVariableStart(description[nameStart])) {
            expanded.push_back(description[index]);
            ++index;
            continue;
        }

        size_t nameEnd = nameStart + 1;
        while (nameEnd < description.size() && isVariableCharacter(description[nameEnd])) {
            ++nameEnd;
        }

        const std::string variableName = description.substr(nameStart, nameEnd - nameStart);
        const char *rawValue = std::getenv(variableName.c_str());
        const std::string value = rawValue ? std::string(rawValue) : std::string();

        if (nameEnd < description.size() && description[nameEnd] == '}') {
            expanded += value;
            index = nameEnd + 1;
            continue;
        }

        if (nameEnd + 2 < description.size() && description[nameEnd] == ':' &&
            description[nameEnd + 1] == '-') {
            const size_t defaultStart = nameEnd + 2;
            const size_t end = description.find('}', defaultStart);
            if (end == std::string::npos) {
                return tl::unexpected(OPK_ERROR(
                    ErrorFlag::ParseError,
                    fmt::format("Unterminated pipeline variable default for '{}'", variableName)));
            }

            expanded +=
                value.empty() ? description.substr(defaultStart, end - defaultStart) : value;
            index = end + 1;
            continue;
        }

        if (nameEnd + 1 < description.size() && description[nameEnd] == '?') {
            const size_t messageStart = nameEnd + 1;
            const size_t end = description.find('}', messageStart);
            if (end == std::string::npos) {
                return tl::unexpected(OPK_ERROR(
                    ErrorFlag::ParseError,
                    fmt::format("Unterminated required pipeline variable '{}'", variableName)));
            }

            if (value.empty()) {
                const std::string message = description.substr(messageStart, end - messageStart);
                return tl::unexpected(OPK_ERROR(
                    ErrorFlag::InvalidData,
                    message.empty()
                        ? fmt::format("Environment variable '{}' is required", variableName)
                        : message));
            }

            expanded += value;
            index = end + 1;
            continue;
        }

        expanded.push_back(description[index]);
        ++index;
    }

    return expanded;
}

} // namespace opk
