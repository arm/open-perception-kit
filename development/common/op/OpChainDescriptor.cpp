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

} // namespace

pek::Result<OpChainDescriptor> OpChainDescriptor::fromJson(const std::string &jsonString,
                                                           const std::string &source) {
    auto result = pek::config::validateOpChainJson(jsonString, source);
    if (!result.has_value())
        return tl::unexpected(PEK_ERROR(pek::ErrorFlag::InvalidData, result.error().toText()));
    return std::move(*result).intoValue();
}

pek::Result<OpChainDescriptor> OpChainDescriptor::fromFile(const std::string &path) {
    std::string content = pek::fs::loadTextOrDefault(path, "");
    auto descriptor = fromJson(content, path);
    if (descriptor.has_value())
        resolveModelDescriptors(*descriptor, path);
    return descriptor;
}
