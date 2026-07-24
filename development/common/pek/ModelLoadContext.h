/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <stop_token>

namespace pek {

/**
 * @brief Optional cooperative cancellation for synchronous model materialization.
 *
 * Loading remains synchronous. Callers choose the execution thread and may supply
 * a stop token. The context must remain alive and unmodified until the loading call
 * returns.
 */
struct ModelLoadContext {
    std::stop_token stopToken;

    [[nodiscard]] bool stopRequested() const noexcept {
        return stopToken.stop_requested();
    }
};

} // namespace pek
