/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <functional>
#include <optional>
#include <stop_token>
#include <string>

namespace pek {

/**
 * @brief State of one model artifact materialization operation.
 */
enum class ModelLoadProgressState { Started, InProgress, Completed, Failed, Unknown };

/**
 * @brief Progress snapshot reported while a model artifact is materialized.
 *
 * Byte counts are decimal strings because modelfetch exposes arbitrary-precision
 * unsigned values. Optional fields are absent when the backend did not report them.
 */
struct ModelLoadProgress {
    std::string artifactId;
    ModelLoadProgressState state = ModelLoadProgressState::Unknown;
    std::optional<std::string> transferredBytes;
    std::optional<std::string> totalBytes;
    std::optional<double> percentage;
};

/**
 * @brief Optional cooperative controls for synchronous model materialization.
 *
 * Loading remains synchronous. Callers choose the execution thread and may supply
 * a stop token and progress callback. The callback may run on a modelfetch worker
 * thread and must therefore be thread-safe. The context must remain alive and
 * unmodified until the loading call returns.
 */
struct ModelLoadContext {
    std::stop_token stopToken;
    std::function<void(const ModelLoadProgress &)> progress;

    [[nodiscard]] bool stopRequested() const noexcept {
        return stopToken.stop_requested();
    }
};

} // namespace pek
