/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

#include "opk/ModelDescriptor.h"
#include "Validator.h"
#include "fmt/color.h"
#include "opk/File.h"
#include "opk/Result.h"
#include "tl/expected.hpp"

#include <filesystem>
#include <utility>

using namespace opk;

opk::Result<ModelDescriptor> ModelDescriptor::fromJson(const std::string &jsonString,
                                                       const std::string &source) {
    auto result = opk::config::validateModelJson(jsonString, source);
    if (!result.has_value())
        return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData, result.error().toText()));
    return std::move(*result);
}

opk::Result<ModelDescriptor> ModelDescriptor::fromFile(const std::string &path) {
    std::string content = opk::fs::loadTextOrDefault(path, "");

    if (content.empty()) {
        return tl::unexpected(
            OPK_ERROR(opk::ErrorFlag::InvalidData,
                      fmt::format("ModelDescriptor file [{}] not found or empty", path)));
    }

    auto descriptor = fromJson(content, path);
    if (descriptor.has_value()) {
        descriptor->modelFile =
            (std::filesystem::path(path).parent_path() / descriptor->modelFile).string();
    }
    return descriptor;
}
