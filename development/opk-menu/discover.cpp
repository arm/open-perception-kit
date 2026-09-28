/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

#include "discover.hpp"

#include "op/OpChainDescriptor.h"
#include "opk/ModelDescriptor.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace opk::menu {
namespace {

std::string normalized_absolute_path(const fs::path &path) {
    return fs::absolute(path).lexically_normal().string();
}

std::string lowercase_extension(const fs::path &path) {
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return extension;
}

bool is_supported_model_file(const fs::path &path) {
    const auto extension = lowercase_extension(path);
    return extension == ".onnx" || extension == ".pte" || extension == ".pt";
}

bool is_executorch_model_file(const fs::path &path) {
    const auto extension = lowercase_extension(path);
    return extension == ".pte" || extension == ".pt";
}

bool file_exists(const fs::path &path) {
    std::error_code error;
    const bool exists = fs::exists(path, error);
    if (!exists || error)
        return false;

    error.clear();
    const bool is_regular_file = fs::is_regular_file(path, error);
    return is_regular_file && !error;
}

bool is_supported_media_file(const fs::path &path) {
    const auto extension = lowercase_extension(path);
    return extension == ".jpg" || extension == ".jpeg" || extension == ".png" ||
           extension == ".bmp" || extension == ".webp" || extension == ".gif" ||
           extension == ".tif" || extension == ".tiff" || extension == ".mov" ||
           extension == ".mp4" || extension == ".m4v" || extension == ".mpg" ||
           extension == ".mpeg" || extension == ".avi" || extension == ".mkv" ||
           extension == ".webm" || extension == ".wmv" || extension == ".flv" ||
           extension == ".ts" || extension == ".m2ts" || extension == ".3gp" ||
           extension == ".y4m" || extension == ".h264" || extension == ".h265" ||
           extension == ".hevc" || extension == ".wav" || extension == ".mp3" ||
           extension == ".aac" || extension == ".flac" || extension == ".ogg" ||
           extension == ".opus";
}

std::string remove_query_or_fragment(std::string_view value) {
    const auto marker_position = value.find_first_of("?#");
    if (marker_position == std::string_view::npos)
        return std::string{value};
    return std::string{value.substr(0, marker_position)};
}

bool is_supported_media_reference(std::string_view value) {
    return is_supported_media_file(fs::path{remove_query_or_fragment(value)});
}

char consume_character(std::string_view text, size_t &position) {
    const char character = text[position];
    ++position;
    return character;
}

void append_quoted_token_part(std::string &token,
                              std::string_view pipeline,
                              size_t &position,
                              char quote) {
    while (position < pipeline.size()) {
        const char quoted_character = consume_character(pipeline, position);
        if (quoted_character == quote)
            return;

        if (quoted_character == '\\' && position < pipeline.size()) {
            token.push_back(consume_character(pipeline, position));
        } else {
            token.push_back(quoted_character);
        }
    }
}

void append_token_character(std::string &token, std::string_view pipeline, size_t &position) {
    const char character = consume_character(pipeline, position);
    if (character == '"' || character == '\'') {
        append_quoted_token_part(token, pipeline, position, character);
        return;
    }

    if (character == '\\' && position < pipeline.size()) {
        token.push_back(consume_character(pipeline, position));
        return;
    }

    token.push_back(character);
}

std::string read_pipeline_token(std::string_view pipeline, size_t &position) {
    std::string token;
    while (position < pipeline.size() &&
           !std::isspace(static_cast<unsigned char>(pipeline[position]))) {
        append_token_character(token, pipeline, position);
    }
    return token;
}

std::vector<std::string> split_pipeline_tokens(std::string_view pipeline) {
    std::vector<std::string> tokens;
    size_t position = 0;
    while (position < pipeline.size()) {
        while (position < pipeline.size() &&
               std::isspace(static_cast<unsigned char>(pipeline[position]))) {
            ++position;
        }
        if (position >= pipeline.size())
            break;

        std::string token = read_pipeline_token(pipeline, position);
        if (!token.empty())
            tokens.push_back(std::move(token));
    }

    return tokens;
}

std::string property_value(std::string_view token, std::string_view property) {
    const auto separator_position = token.find('=');
    if (separator_position == std::string_view::npos)
        return {};
    if (separator_position != property.size() || !token.starts_with(property))
        return {};
    return std::string{token.substr(separator_position + 1)};
}

std::string value_part(std::string_view token) {
    const auto separator_position = token.find('=');
    if (separator_position == std::string_view::npos)
        return {};
    return std::string{token.substr(separator_position + 1)};
}

std::string_view property_name(std::string_view token) {
    const auto separator_position = token.find('=');
    if (separator_position == std::string_view::npos)
        return {};
    return token.substr(0, separator_position);
}

std::string strip_file_uri(std::string value) {
    constexpr std::string_view file_uri_prefix = "file://";
    if (!std::string_view{value}.starts_with(file_uri_prefix))
        return value;

    value.erase(0, file_uri_prefix.size());
    if (!value.empty() && value.front() != '/')
        return {};
    return value;
}

bool is_source_element(std::string_view element) {
    return element.ends_with("src") || element == "uridecodebin" || element == "urisourcebin" ||
           element == "playbin";
}

bool is_input_media_property(std::string_view element, std::string_view property) {
    if (property == "bg-image")
        return element == "opkosd";
    if (property == "location")
        return is_source_element(element);
    if (property == "uri")
        return is_source_element(element);
    return false;
}

} // namespace

std::vector<std::string> discover_opchain_paths(std::string_view pipeline) {
    std::set<std::string, std::less<>> seen_paths;
    std::vector<std::string> paths;

    for (const auto &token : split_pipeline_tokens(pipeline)) {
        auto path = property_value(token, "opchain-path");
        if (!path.empty() && seen_paths.insert(path).second)
            paths.push_back(std::move(path));
    }

    return paths;
}

opk::Result<std::vector<std::string>> discover_model_files(const std::string &opchain_path) {
    auto opchain =
        opk::op::OpChainDescriptor::fromFile(normalized_absolute_path(fs::path{opchain_path}));
    if (!opchain.has_value())
        return tl::unexpected(std::move(opchain.error()));

    std::set<std::string, std::less<>> seen_model_files;
    std::vector<std::string> model_files;

    for (const auto &op : opchain->ops) {
        if (!opk::op::isInferenceOpId(op.id) || !op.attributes.contains("modelDescriptor"))
            continue;

        auto model = opk::ModelDescriptor::fromFile(op.attributes.getString("modelDescriptor"));
        if (!model.has_value())
            return tl::unexpected(std::move(model.error()));

        const auto model_file = normalized_absolute_path(fs::path{model->modelFile});
        if (!is_supported_model_file(fs::path{model_file}))
            continue;

        if (seen_model_files.insert(model_file).second)
            model_files.push_back(model_file);
    }

    return model_files;
}

ExecuTorchDependencyStatus check_executorch_dependency(const std::vector<std::string> &model_files,
                                                       const std::string &plugin_directory) {
    constexpr std::string_view PluginFileName = "opk-executorch-ops.so";
    ExecuTorchDependencyStatus status;

    for (const auto &model_file : model_files) {
        if (is_executorch_model_file(model_file)) {
            status.required = true;
            break;
        }
    }

    if (!status.required)
        return status;

    status.pluginPath =
        normalized_absolute_path(fs::path{plugin_directory} / std::string{PluginFileName});
    status.pluginFound = file_exists(status.pluginPath);
    return status;
}

bool is_remote_reference(std::string_view value) {
    const auto scheme_position = value.find("://");
    return scheme_position != std::string_view::npos && !value.starts_with("file://");
}

std::vector<std::string> discover_media_files(std::string_view pipeline) {
    std::set<std::string, std::less<>> seen_media_files;
    std::vector<std::string> media_files;
    std::string_view element;

    for (const auto &token : split_pipeline_tokens(pipeline)) {
        if (token == "!") {
            element = {};
            continue;
        }
        if (element.empty()) {
            element = token;
            continue;
        }

        const auto property = property_name(token);
        if (!is_input_media_property(element, property))
            continue;

        auto candidate = value_part(token);
        if (candidate.empty())
            continue;

        if (is_remote_reference(candidate)) {
            if (is_supported_media_reference(candidate) &&
                seen_media_files.insert(candidate).second)
                media_files.push_back(std::move(candidate));
            continue;
        }

        candidate = strip_file_uri(std::move(candidate));
        if (candidate.empty() || !is_supported_media_reference(candidate))
            continue;

        const fs::path candidate_path{candidate};
        const auto media_file = normalized_absolute_path(candidate_path);
        if (seen_media_files.insert(media_file).second)
            media_files.push_back(media_file);
    }

    return media_files;
}

} // namespace opk::menu
