/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "pek/ModelDescriptor.h"
#include "Validator.h"
#include "fmt/color.h"
#include "pek/File.h"
#include "pek/Result.h"
#include "tl/expected.hpp"

#include <filesystem>
#include <utility>

using namespace pek;

pek::Result<ModelDescriptor> ModelDescriptor::fromJson(const std::string &jsonString,
                                                       const std::string &source) {
    auto result = pek::config::validateModelJson(jsonString, source);
    if (!result.has_value())
        return tl::unexpected(PEK_ERROR(pek::ErrorFlag::InvalidData, result.error().toText()));
    return std::move(*result);
}

pek::Result<ModelDescriptor> ModelDescriptor::fromFile(const std::string &path) {
    std::string content = pek::fs::loadTextOrDefault(path, "");

    if (content.empty()) {
        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("ModelDescriptor file [{}] not found or empty", path)));
    }

    auto descriptor = fromJson(content, path);
    if (descriptor.has_value()) {
        descriptor->modelFile =
            (std::filesystem::path(path).parent_path() / descriptor->modelFile).string();
    }
    return descriptor;
}
