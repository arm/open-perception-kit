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

std::filesystem::path resolvePath(const std::filesystem::path &directory,
                                  std::filesystem::path reference) {
    return reference.is_absolute() ? std::move(reference) : directory / reference;
}

void resolvePathAttribute(OpChainDescriptor::Op &op,
                          std::string_view attribute,
                          const std::filesystem::path &directory) {
    const std::string key(attribute);
    if (!op.attributes.contains(key))
        return;
    op.attributes.set(key, resolvePath(directory, op.attributes.getString(key)).string());
}

void resolvePathArrayAttribute(OpChainDescriptor::Op &op,
                               std::string_view attribute,
                               const std::filesystem::path &directory) {
    const std::string key(attribute);
    if (!op.attributes.contains(key))
        return;

    pek::AttributeValue::Array resolved;
    for (const auto &value : op.attributes.getArray(key)) {
        resolved.emplace_back(resolvePath(directory, value.asString()).string());
    }
    op.attributes.setArray(key, std::move(resolved));
}

void resolveDescriptorPaths(OpChainDescriptor &descriptor, const std::filesystem::path &source) {
    const auto directory = source.parent_path();
    for (auto &op : descriptor.ops) {
        if (isInferenceOpId(op.id))
            resolvePathAttribute(op, "modelDescriptor", directory);
        if (op.id == "pek-python-ops/PythonScript") {
            resolvePathAttribute(op, "script", directory);
            resolvePathArrayAttribute(op, "pythonPaths", directory);
        }
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
    if (descriptor.has_value())
        resolveDescriptorPaths(*descriptor, path);
    return descriptor;
}
