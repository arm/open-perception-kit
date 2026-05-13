/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "op/OpChainDescriptor.h"

#include "pek/File.h"

using namespace pek;

pek::Result<OpChainDescriptor> OpChainDescriptor::fromJson(const std::string &jsonString) {

    try {
        json json = json::parse(jsonString);
        return json.get<OpChainDescriptor>();
    } catch (const json::exception &e) {
        return tl::unexpected(PEK_ERROR(
            pek::ErrorFlag::InvalidData,
            fmt::format("Error occured while parsing OpChainDescriptor json: {}", e.what())));
    }
}

pek::Result<OpChainDescriptor> OpChainDescriptor::fromFile(const std::string &path) {
    std::string content = pek::fs::loadTextOrDefault(path, "");
    return fromJson(content);
}
