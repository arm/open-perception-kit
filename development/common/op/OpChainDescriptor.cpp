/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "op/OpChainDescriptor.h"

#include "pek/File.h"

#include <filesystem>

using namespace pek::op;

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
    auto descriptor = fromJson(content);
    if (!descriptor)
        return descriptor;

    // Resolve from the opchain rather than the process working directory so release packages
    // remain relocatable.
    const std::filesystem::path descriptorDirectory = std::filesystem::path(path).parent_path();
    for (auto &op : descriptor->ops) {
        if (!op.attributes.contains("modelDescriptor"))
            continue;

        const auto &value = op.attributes.at("modelDescriptor");
        if (!value.isString())
            continue;

        const std::filesystem::path modelDescriptor(value.asString());
        if (!modelDescriptor.is_absolute()) {
            op.attributes.set("modelDescriptor",
                              (descriptorDirectory / modelDescriptor).lexically_normal().string());
        }
    }
    return descriptor;
}
