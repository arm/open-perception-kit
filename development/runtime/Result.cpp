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
