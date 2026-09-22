/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

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
