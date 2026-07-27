/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "op/OpSetupContext.h"

#include <utility>

namespace pek::op {
namespace {

pek::Error modelResolutionCancelled(const std::string &path) {
    return PEK_ERROR(pek::ErrorFlag::SystemFailure,
                     "Model descriptor resolution cancelled for [" + path + "]");
}

} // namespace

pek::Result<pek::ModelDescriptor> OpSetupContext::resolveModelDescriptor(const std::string &path) {
    if (stopRequested())
        return tl::unexpected{modelResolutionCancelled(path)};

    if (const auto cached = modelDescriptors.find(path); cached != modelDescriptors.end())
        return cached->second;

    auto descriptor = pek::ModelDescriptor::fromFile(path, stopToken);
    if (!descriptor)
        return tl::unexpected{descriptor.error()};
    if (stopRequested())
        return tl::unexpected{modelResolutionCancelled(path)};

    const auto stored = modelDescriptors.emplace(path, std::move(*descriptor)).first;
    return stored->second;
}

} // namespace pek::op
