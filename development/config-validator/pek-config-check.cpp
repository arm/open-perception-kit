/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

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
    std::string format = "text";
    bool help = false;
};

constexpr std::string_view Usage =
    "Usage: pek-config-check --root <repo-root> [--format text|json]\n";

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
        if (option == "--format" && index < argc) {
            arguments.format = argv[index++];
            continue;
        }
        return tl::unexpected{std::format("unknown or incomplete argument: {}", option)};
    }
    if (arguments.root.empty())
        return tl::unexpected{std::string("--root is required")};
    if (arguments.format != "text" && arguments.format != "json")
        return tl::unexpected{std::string("--format must be text or json")};
    return arguments;
}

} // namespace

int main(int argc, char **argv) {
    auto arguments = parseArguments(argc, argv);
    if (!arguments.has_value()) {
        pek::log::error("pek-config-check: {}\n{}", arguments.error(), Usage);
        pek::log::flush();
        return 2;
    }
    if (arguments->help) {
        pek::log::instantInfo("{}", Usage);
        return 0;
    }
    if (std::error_code error; !std::filesystem::is_directory(arguments->root, error)) {
        pek::log::error("pek-config-check: repository root is not a directory\n");
        pek::log::flush();
        return 2;
    }

    const pek::config::ValidationReport report =
        pek::config::detail::validateRepository(arguments->root);
    const std::string output = arguments->format == "json" ? report.toJson() : report.toText();
    if (report.ok() || arguments->format == "json") {
        pek::log::instantInfo("{}", output);
    } else {
        pek::log::error("{}", output);
        pek::log::flush();
    }
    return report.ok() ? 0 : 1;
}
