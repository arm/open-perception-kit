/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

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
