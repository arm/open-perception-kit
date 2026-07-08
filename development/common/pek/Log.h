/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/String.h"

#include <cstddef>
#include <cstdio>
#include <fmt/format.h>
#include <optional>
#include <string_view>
#include <utility>

namespace pek {

// Log levels are ordered by increasing verbosity. A configured level includes messages at that
// level and every less verbose level below it.
enum class LogLevel : int { Off = 0, Error = 1, Warn = 2, Notice = 3, Info = 4 };

constexpr int log_level_value(LogLevel level) {
    return static_cast<int>(level);
}

// The default is used when the environment variable does not exist or is malformed.
static constexpr LogLevel defaultLogLevel{LogLevel::Info};

struct LogTools {

    static constexpr std::string InvOn = "\033[7m";
    static constexpr std::string InvOff = "\033[0m";

    static std::string invert(const std::string &text) {
        return InvOn + text + InvOff;
    }

    static std::string enframe(const std::string &text, const std::string &title = "") {
        std::string result;

        auto lines = pek::utf8::split(text, "\n");

        size_t titleLength = pek::utf8::length(title);
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
                while (pek::utf8::length(middleLine) < maxLineLength + 1)
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

std::optional<int> parse_log_level(std::string_view value);
int current_log_level();
void set_log_level(int logLevel);

namespace detail {

void write_stdout(fmt::string_view msg);
void write_stderr(fmt::string_view msg);

} // namespace detail

inline bool should_log(LogLevel lvl) {
    return lvl != LogLevel::Off && current_log_level() >= log_level_value(lvl);
}

// Single chokepoint for filtering, routing, and prefixing. The implementation lives in the common
// library so later sink changes apply consistently to every caller, including release builds.
void log_write(LogLevel lvl, fmt::string_view msg);

inline void log_flush() {
    std::fflush(stdout);
    std::fflush(stderr);
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

// Unconditional single-stream output for command-line interfaces and other output that is part
// of a program's contract. Unlike the severity-based functions above, these functions are not
// affected by the configured log level and do not add prefixes
template <typename... Args>
inline void force_log(fmt::format_string<Args...> fmtstr, Args &&...args) {
    auto s = fmt::format(fmtstr, std::forward<Args>(args)...);
    detail::write_stdout(s);
}

template <typename... Args>
inline void force_loge(fmt::format_string<Args...> fmtstr, Args &&...args) {
    auto s = fmt::format(fmtstr, std::forward<Args>(args)...);
    detail::write_stderr(s);
}

// --- Optional runtime-format API ---
// Use this only when the format string is not a literal / not known at compile-time.
// Named differently to avoid overload ambiguity with string literals.
template <typename... Args> inline void log_runtime(fmt::string_view fmtstr, Args &&...args) {
    auto s = fmt::vformat(fmtstr, fmt::make_format_args(args...));
    log_write(LogLevel::Info, s);
}

template <typename... Args> inline void logw_runtime(fmt::string_view fmtstr, Args &&...args) {
    auto s = fmt::vformat(fmtstr, fmt::make_format_args(args...));
    log_write(LogLevel::Warn, s);
}

template <typename... Args> inline void loge_runtime(fmt::string_view fmtstr, Args &&...args) {
    auto s = fmt::vformat(fmtstr, fmt::make_format_args(args...));
    log_write(LogLevel::Error, s);
}

} // namespace pek
