/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "LogTools.h"

#include "pek/String.h"

#include <cstddef>

namespace pek::log::LogTools {

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

} // namespace pek::log::LogTools
