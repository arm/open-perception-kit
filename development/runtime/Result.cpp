/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "runtime/Result.h"

#include <fmt/core.h>

#include <utility>

#include <magic_enum/magic_enum.hpp>

namespace opk::runtime {

Error::Error(ErrorFlag f, std::string i, std::source_location loc)
    : flag(f), info(std::move(i)), file(loc.file_name()), function(loc.function_name()),
      line(loc.line()) {}

std::string Error::toString() const {
    std::string ret;
    ret += "\n";
    ret += "Error:\n";
    ret += fmt::format("{}\n", magic_enum::enum_name(flag));
    ret += "Reason:\n";
    ret += info;

    if (!file.empty()) {
        ret += "\nWhere\n";
        ret += fmt::format("{}\n{}\n{}", file, function, line);
    }

    return ret;
}

} // namespace opk::runtime
