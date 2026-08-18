/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "op/OpChainDescriptor.h"

#include "Validator.h"
#include "pek/File.h"

#include <filesystem>
#include <utility>

using namespace pek::op;

namespace {

void resolveModelDescriptors(OpChainDescriptor &descriptor, const std::filesystem::path &source) {
    for (auto &op : descriptor.ops) {
        if (!isInferenceOpId(op.id))
            continue;
        const auto reference = std::filesystem::path(op.attributes.getString("modelDescriptor"));
        op.attributes.set("modelDescriptor", (source.parent_path() / reference).string());
    }
}

void resolvePythonScriptPaths(OpChainDescriptor &descriptor, const std::filesystem::path &source) {
    for (auto &op : descriptor.ops) {
        if (op.id != "pek-python-ops/PythonScript")
            continue;

        if (op.attributes.contains("script")) {
            auto reference = std::filesystem::path(op.attributes.getString("script"));
            if (!reference.is_absolute())
                reference = source.parent_path() / reference;
            op.attributes.set("script", reference.string());
        }

        if (!op.attributes.contains("pythonPaths"))
            continue;

        pek::AttributeValue::Array resolved;
        for (const auto &value : op.attributes.getArray("pythonPaths")) {
            auto reference = std::filesystem::path(value.asString());
            if (!reference.is_absolute())
                reference = source.parent_path() / reference;
            resolved.emplace_back(reference.string());
        }
        op.attributes.setArray("pythonPaths", std::move(resolved));
    }
}

} // namespace

pek::Result<OpChainDescriptor> OpChainDescriptor::fromJson(const std::string &jsonString,
                                                           const std::string &source) {
    auto result = pek::config::validateOpChainJson(jsonString, source);
    if (!result.has_value())
        return tl::unexpected(PEK_ERROR(pek::ErrorFlag::InvalidData, result.error().toText()));
    return std::move(*result);
}

pek::Result<OpChainDescriptor> OpChainDescriptor::fromFile(const std::string &path) {
    std::string content = pek::fs::loadTextOrDefault(path, "");
    auto descriptor = fromJson(content, path);
    if (descriptor.has_value()) {
        resolveModelDescriptors(*descriptor, path);
        resolvePythonScriptPaths(*descriptor, path);
    }
    return descriptor;
}
