/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "op/OpChainDescriptor.h"

#include "Validator.h"
#include "pek/File.h"

#include <algorithm>
#include <filesystem>
#include <set>
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

bool hasRelativePathAttribute(const OpChainDescriptor::Op &op, std::string_view attribute) {
    const std::string key(attribute);
    return op.attributes.contains(key) &&
           !std::filesystem::path(op.attributes.getString(key)).is_absolute();
}

bool hasRelativePathArrayAttribute(const OpChainDescriptor::Op &op, std::string_view attribute) {
    const std::string key(attribute);
    if (!op.attributes.contains(key))
        return false;

    return std::ranges::any_of(op.attributes.getArray(key), [](const auto &value) {
        return !std::filesystem::path(value.asString()).is_absolute();
    });
}

pek::Result<void> resolveDescriptorPaths(OpChainDescriptor &descriptor,
                                         const std::filesystem::path &source) {
    const auto descriptorDirectory = source.parent_path();
    std::set<std::filesystem::path> modelDirectories;

    for (auto &op : descriptor.ops) {
        if (!isInferenceOpId(op.id))
            continue;
        resolvePathAttribute(op, "modelDescriptor", descriptorDirectory);
        modelDirectories.emplace(
            std::filesystem::path(op.attributes.getString("modelDescriptor")).parent_path());
    }

    for (auto &op : descriptor.ops) {
        if (op.id != "pek-python-ops/PythonScript")
            continue;

        if (const bool needsModelDirectory = hasRelativePathAttribute(op, "script") ||
                                             hasRelativePathArrayAttribute(op, "pythonPaths");
            !needsModelDirectory)
            continue;
        if (modelDirectories.empty()) {
            return tl::unexpected(
                PEK_ERROR(pek::ErrorFlag::InvalidData,
                          "relative PythonScript paths require an inference modelDescriptor"));
        }
        if (modelDirectories.size() != 1) {
            return tl::unexpected(PEK_ERROR(
                pek::ErrorFlag::InvalidData,
                "relative PythonScript paths are ambiguous with modelDescriptors in multiple "
                "directories; use absolute paths"));
        }

        const auto &modelDirectory = *modelDirectories.begin();
        resolvePathAttribute(op, "script", modelDirectory);
        resolvePathArrayAttribute(op, "pythonPaths", modelDirectory);
    }

    return {};
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
        auto resolution = resolveDescriptorPaths(*descriptor, path);
        if (!resolution.has_value())
            return tl::unexpected(std::move(resolution.error()));
    }
    return descriptor;
}
