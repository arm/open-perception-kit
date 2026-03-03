/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "op/OpChainDescriptor.h"

#include "amp/File.h"

using namespace amp;

amp::Result<OpChainDescriptor> OpChainDescriptor::fromJson(const std::string &jsonString) {

    try {
        json json = json::parse(jsonString);
        return json.get<OpChainDescriptor>();
    } catch (const json::exception &e) {
        return tl::unexpected(AMP_ERROR(
            amp::ErrorFlag::InvalidData,
            fmt::format("Error occured while parsing OpChainDescriptor json: {}", e.what())));
    }
}

amp::Result<OpChainDescriptor> OpChainDescriptor::fromFile(const std::string &path) {
    std::string content = amp::fs::loadTextOrDefault(path, "");
    return fromJson(content);
}
