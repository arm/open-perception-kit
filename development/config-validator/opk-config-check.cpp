/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

#include "Log.h"
#include "RepositoryValidatorInternal.h"

#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <tl/expected.hpp>

namespace {

struct Arguments {
    std::filesystem::path root;
    bool help = false;
};

constexpr std::string_view Usage = "Usage: opk-config-check --root <repo-root>\n";

tl::expected<Arguments, std::string> parseArguments(int argc, char **argv) {
    Arguments arguments;
    int index = 1;
    while (index < argc) {
        const std::string option = argv[index++];
        if (option == "--help") {
            arguments.help = true;
            return arguments;
        }
        if (option == "--root" && index < argc) {
            arguments.root = argv[index++];
            continue;
        }
        return tl::unexpected{std::format("unknown or incomplete argument: {}", option)};
    }
    if (arguments.root.empty())
        return tl::unexpected{std::string("--root is required")};
    return arguments;
}

} // namespace

int main(int argc, char **argv) {
    auto arguments = parseArguments(argc, argv);
    if (!arguments.has_value()) {
        opk::log::error("opk-config-check: {}\n", arguments.error());
        opk::log::error(Usage);
        opk::log::flush();
        return 2;
    }
    if (arguments->help) {
        opk::log::instantInfo("{}", Usage);
        return 0;
    }
    if (std::error_code error; !std::filesystem::is_directory(arguments->root, error)) {
        opk::log::error("opk-config-check: repository root is not a directory\n");
        opk::log::flush();
        return 2;
    }

    const opk::config::ValidationReport report =
        opk::config::detail::validateRepository(arguments->root);
    if (report.ok()) {
        opk::log::instantInfo("{}", report.toText());
    } else {
        for (const auto &issue : report.issues) {
            opk::log::error("{}\n", issue.toText());
        }
        opk::log::flush();
    }
    return report.ok() ? 0 : 1;
}
