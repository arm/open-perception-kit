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

#include "tools.h"

#include "opk/String.h"

#include <cstddef>

namespace opk::log::tools {

std::string enframe(const std::string &text, const std::string &title) {
    std::string result;
    auto lines = utf8::split(text, "\n");

    const std::size_t titleLength = utf8::length(title);
    std::size_t maxLineLength = 0;
    for (const auto &line : lines) {
        if (utf8::length(line) > maxLineLength) {
            maxLineLength = utf8::length(line);
        }
    }
    if (title.length() + 2 > maxLineLength) {
        maxLineLength = title.length() + 2;
    }

    std::string upperBorder = "╭─" + invert(title);
    for (std::size_t index = titleLength + 1; index < maxLineLength; ++index) {
        upperBorder += "─";
    }
    upperBorder += "╮\n";
    result += upperBorder;

    while (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }
    while (!lines.empty() && lines.front().empty()) {
        lines.erase(lines.begin());
    }

    std::string middleLine;
    bool previousWasEmpty = false;
    for (const auto &line : lines) {
        if (utf8::trim(line).empty()) {
            if (previousWasEmpty) {
                continue;
            }
            previousWasEmpty = true;
            middleLine = "├" + line;
            for (std::size_t index = 0; index < maxLineLength; ++index) {
                middleLine += "─";
            }
            middleLine += "┤";
        } else {
            previousWasEmpty = false;
            middleLine = "│" + line;
            while (utf8::length(middleLine) < maxLineLength + 1) {
                middleLine += " ";
            }
            middleLine += "│";
        }
        result += middleLine + "\n";
    }

    std::string lowerBorder = "╰";
    for (std::size_t index = 0; index < maxLineLength; ++index) {
        lowerBorder += "─";
    }
    lowerBorder += "╯";
    result += lowerBorder + "\n";

    return result;
}

std::string invert(std::string_view text) {
    return "\033[7m" + std::string(text) + "\033[0m";
}

} // namespace opk::log::tools
