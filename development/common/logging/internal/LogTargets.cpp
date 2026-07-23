/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "LogTargets.h"

#include "ConsoleOutputs.h"

#include <algorithm>
#include <memory>

namespace pek::logging {

LogTargets createBuiltInLogTargets(const std::vector<LogTargetType> &enabledTargets) {
    const auto isEnabled = [&enabledTargets](LogTargetType type) {
        return std::find(enabledTargets.begin(), enabledTargets.end(), type) !=
               enabledTargets.end();
    };

    LogTargets targets;
    targets.push_back(std::make_unique<ConsoleOutput>(
        LogTargetType::Stdout, isEnabled(LogTargetType::Stdout), stdout));
    targets.push_back(std::make_unique<ConsoleOutput>(
        LogTargetType::Stderr, isEnabled(LogTargetType::Stderr), stderr));
    return targets;
}

} // namespace pek::logging
