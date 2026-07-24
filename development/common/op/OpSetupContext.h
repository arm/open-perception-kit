/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/ModelDescriptor.h"
#include "pek/ModelLoadContext.h"
#include "pek/Result.h"

#include <string>
#include <unordered_map>
#include <utility>

namespace pek::op {

/**
 * @brief Shared controls and resolved model descriptors for one OpChain setup.
 *
 * The context is scoped to one setup attempt. Operations may resolve model
 * descriptors through it so cancellation reaches the real materialization call.
 * Successful resolutions are cached for the remainder of that setup attempt.
 */
class OpSetupContext {
  public:
    explicit OpSetupContext(pek::ModelLoadContext modelLoadContextValue = {})
        : modelLoadContext(std::move(modelLoadContextValue)) {}

    /**
     * @brief Resolves and materializes a model descriptor for this setup attempt.
     *
     * Successful results are cached by the descriptor path supplied by the
     * operation. Failed or cancelled results are not cached.
     *
     * @param path Path to the model descriptor.
     * @return A descriptor whose modelFile is a verified local path, or an error.
     */
    pek::Result<pek::ModelDescriptor> resolveModelDescriptor(const std::string &path);

    /**
     * @brief Reports whether cancellation was requested for this setup attempt.
     */
    [[nodiscard]] bool stopRequested() const noexcept {
        return modelLoadContext.stopRequested();
    }

  private:
    pek::ModelLoadContext modelLoadContext;
    std::unordered_map<std::string, pek::ModelDescriptor> modelDescriptors;
};

} // namespace pek::op
