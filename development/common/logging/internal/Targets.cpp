/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Targets.h"

#include "ConsoleOutputs.h"
#include "FileOutput.h"

#include <algorithm>
#include <memory>

namespace opk::log {

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

} // namespace opk::log
