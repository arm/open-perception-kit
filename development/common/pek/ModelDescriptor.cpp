/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "pek/ModelDescriptor.h"
#include "fmt/color.h"
#include "tl/expected.hpp"

#include "pek/File.h"
#include "pek/Result.h"

#include "pek/AttributeMap.h"

using namespace pek;

pek::Result<ModelDescriptor> ModelDescriptor::fromJson(const std::string &jsonString) {
    try {
        json json = json::parse(jsonString);
        return json.get<ModelDescriptor>();
    } catch (const json::exception &e) {
        return tl::unexpected(PEK_ERROR(
            pek::ErrorFlag::InvalidData,
            fmt::format("Error occured while parsing ModelDescriptor json: {}", e.what())));
    }
}

pek::Result<ModelDescriptor> ModelDescriptor::fromFile(const std::string &path) {
    std::string content = pek::fs::loadTextOrDefault(path, "");

    if (content.empty()) {
        return tl::unexpected(
            PEK_ERROR(pek::ErrorFlag::InvalidData,
                      fmt::format("ModelDescriptor file [{}] not found or empty", path)));
    }

    return fromJson(content);
}
