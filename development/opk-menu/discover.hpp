/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

#pragma once

#include "opk/Result.h"

#include <string>
#include <string_view>
#include <vector>

namespace opk::menu {

struct ExecuTorchDependencyStatus {
    bool required{};
    bool pluginFound{};
    std::string pluginPath{};
};

[[nodiscard]] std::vector<std::string> discover_opchain_paths(std::string_view pipeline);

[[nodiscard]] opk::Result<std::vector<std::string>>
discover_model_files(const std::string &opchain_path);

[[nodiscard]] ExecuTorchDependencyStatus
check_executorch_dependency(const std::vector<std::string> &model_files,
                            const std::string &plugin_directory);

[[nodiscard]] bool is_remote_reference(std::string_view value);

[[nodiscard]] std::vector<std::string> discover_media_files(std::string_view pipeline);

} // namespace opk::menu
