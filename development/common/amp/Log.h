/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "amp/String.h"

#include <cstddef>
#include <fmt/format.h>
#include <utility>

namespace amp {

struct LogTools {

    static constexpr std::string InvOn = "\033[7m";
    static constexpr std::string InvOff = "\033[0m";

    static std::string invert(const std::string &text) {
        return InvOn + text + InvOff;
    }

    static std::string enframe(const std::string &text, const std::string &title = "") {
        std::string result;

        auto lines = amp::utf8::split(text, "\n");

        size_t titleLength = amp::utf8::length(title);
        size_t maxLineLength = 0;
        for (const auto &line : lines) {
            if (utf8::length(line) > maxLineLength) {
                maxLineLength = utf8::length(line);
            }
        }
        if (title.length() + 2 > maxLineLength) {
            maxLineLength = title.length() + 2;
        }

        // up
        std::string upperBorder = "╭";
        upperBorder += "─" + InvOn + title + InvOff;
        for (size_t i = titleLength + 1; i < maxLineLength; i++)
            upperBorder += "─";
        upperBorder += "╮\n";

        result += upperBorder;

        // remove the last if empty line
        while (!lines.empty() && lines.back().empty())
            lines.pop_back();

        // remove the first if empty line
        while (!lines.empty() && lines.front().empty())
            lines.erase(lines.begin());

        // middle
        std::string middleLine;
        bool prevWasEmpty = false;
        for (const auto &line : lines) {
            // insert horizontal line if the line is empty
            if (utf8::trim(line).empty()) {
                if (prevWasEmpty) {
                    continue;
                } else {
                    prevWasEmpty = true;
                    middleLine = "├" + line;
                    for (size_t i = 0; i < maxLineLength; i++)
                        middleLine += "─";
                    middleLine += "┤";
                }
            } else {
                prevWasEmpty = false;
                middleLine = "│" + line;
                while (amp::utf8::length(middleLine) < maxLineLength + 1)
                    middleLine += " ";
                middleLine += "│";
            }

            result += middleLine + "\n";
        }

        // down
        std::string lowerBorder = "╰";
        for (size_t i = 0; i < maxLineLength; i++)
            lowerBorder += "─";
        lowerBorder += "╯";
        result += lowerBorder + "\n";

        return result;
    }
};

} // namespace amp

namespace amp {

// extend later (channels/sinks)
enum class LogLevel { Info, Notice, Warn, Error };

// single chokepoint for output routing/prefixing
// later we can add timestamps, thread id, channel, etc. here
inline void log_write(LogLevel lvl, fmt::string_view msg) {
    switch (lvl) {
    case LogLevel::Info:
        fmt::print("{}", msg);
        break;
    case LogLevel::Notice:
        fmt::print("{}", LogTools::invert(msg.data()));
        break;
    case LogLevel::Warn:
        fmt::print("W: {}", msg);
        break;
    case LogLevel::Error:
        fmt::print("E: {}", msg);
        break;
    }
}

// --- Primary API: compile-time checked formatting (drop-in for fmt::print) ---
// This is what you want to use in normal code.
// It avoids the ambiguity you hit with a string_view overload.
template <typename... Args> inline void log(fmt::format_string<Args...> fmtstr, Args &&...args) {
    // Long-string-proof (dynamic allocation as needed)
    auto s = fmt::format(fmtstr, std::forward<Args>(args)...);
    log_write(LogLevel::Info, s);
}

template <typename... Args> inline void logn(fmt::format_string<Args...> fmtstr, Args &&...args) {
    auto s = fmt::format(fmtstr, std::forward<Args>(args)...);
    log_write(LogLevel::Notice, s);
}

template <typename... Args> inline void logw(fmt::format_string<Args...> fmtstr, Args &&...args) {
    auto s = fmt::format(fmtstr, std::forward<Args>(args)...);
    log_write(LogLevel::Warn, s);
}

template <typename... Args> inline void loge(fmt::format_string<Args...> fmtstr, Args &&...args) {
    auto s = fmt::format(fmtstr, std::forward<Args>(args)...);
    log_write(LogLevel::Error, s);
}

// --- Optional runtime-format API ---
// Use this only when the format string is not a literal / not known at compile-time.
// Named differently to avoid overload ambiguity with string literals.
template <typename... Args> inline void log_runtime(fmt::string_view fmtstr, Args &&...args) {
    auto s = fmt::vformat(fmtstr, fmt::make_format_args(std::forward<Args>(args)...));
    log_write(LogLevel::Info, s);
}

template <typename... Args> inline void logw_runtime(fmt::string_view fmtstr, Args &&...args) {
    auto s = fmt::vformat(fmtstr, fmt::make_format_args(std::forward<Args>(args)...));
    log_write(LogLevel::Warn, s);
}

template <typename... Args> inline void loge_runtime(fmt::string_view fmtstr, Args &&...args) {
    auto s = fmt::vformat(fmtstr, fmt::make_format_args(std::forward<Args>(args)...));
    log_write(LogLevel::Error, s);
}

} // namespace amp
