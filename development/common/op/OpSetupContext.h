/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/ModelDescriptor.h"
#include "pek/Result.h"

#include <cstddef>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>
#include <unordered_map>

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
    explicit OpSetupContext(std::stop_token stopTokenValue = {}) : stopToken(stopTokenValue) {}

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
        return stopToken.stop_requested();
    }

  private:
    struct TransparentStringHash {
        using is_transparent = void;

        [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept {
            return std::hash<std::string_view>{}(value);
        }
    };

    std::stop_token stopToken;
    std::unordered_map<std::string, pek::ModelDescriptor, TransparentStringHash, std::equal_to<>>
        modelDescriptors;
};

} // namespace pek::op
