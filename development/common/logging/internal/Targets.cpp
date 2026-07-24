/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "Targets.h"

#include "ConsoleOutputs.h"
#include "FileOutput.h"

#include <algorithm>
#include <memory>

namespace pek::log {

Targets createBuiltInLogTargets(const std::vector<TargetType> &enabledTargets,
                                const std::string &logFileName) {
    const auto isEnabled = [&enabledTargets](TargetType type) {
        return std::find(enabledTargets.begin(), enabledTargets.end(), type) !=
               enabledTargets.end();
    };

    Targets targets;
    targets.push_back(
        std::make_unique<ConsoleOutput>(TargetType::Stdout, isEnabled(TargetType::Stdout), stdout));
    targets.push_back(
        std::make_unique<ConsoleOutput>(TargetType::Stderr, isEnabled(TargetType::Stderr), stderr));
    targets.push_back(std::make_unique<FileOutput>(logFileName, isEnabled(TargetType::File)));
    return targets;
}

} // namespace pek::log
