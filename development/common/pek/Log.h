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

// The default is used when the environment variable does not exist or is malformed.
static constexpr LogLevel defaultLogLevel{LogLevel::Info};

struct LogTools {

    static constexpr std::string InvOn = "\033[7m";
    static constexpr std::string InvOff = "\033[0m";

    /// Wraps text in a Unicode frame with an optional title.
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

namespace private_ {

/// Converts a log level to its ordered numeric value.
constexpr int logLevelValue(LogLevel level) {
    return static_cast<int>(level);
}

/// Applies ANSI reverse styling to text.
inline std::string invert(const std::string &text) {
    return LogTools::InvOn + text + LogTools::InvOff;
}

/// Parses a numeric log level, rejecting malformed values and capping excessive values.
std::optional<int> parseLogLevel(std::string_view value);

/// Returns whether a message at the given level passes the configured threshold.
bool shouldLog(LogLevel lvl);

// Single chokepoint for filtering, routing, and prefixing. The implementation lives in the common
// library so later sink changes apply consistently to every caller, including release builds.
void logWrite(LogLevel lvl, fmt::string_view msg);

} // namespace private_

/// Returns the currently configured numeric log level.
int getLogLevel();

/// Sets the numeric log level, clamped to the supported range.
void setLogLevel(int logLevel);

/// Flushes both output streams used by the logging API.
inline void logFlush() {
    std::fflush(stdout);
    std::fflush(stderr);
}

// --- Primary API: compile-time checked formatting (drop-in for fmt::print) ---
// This is what you want to use in normal code.
// It avoids the ambiguity you hit with a string_view overload.
/// Formats and writes an informational message when enabled.
template <typename... Args> inline void log(fmt::format_string<Args...> fmtstr, Args &&...args) {
    // Long-string-proof (dynamic allocation as needed)
    auto s = fmt::format(fmtstr, std::forward<Args>(args)...);
    private_::logWrite(LogLevel::Info, s);
}

/// Formats and writes a notice message when enabled.
template <typename... Args> inline void logn(fmt::format_string<Args...> fmtstr, Args &&...args) {
    auto s = fmt::format(fmtstr, std::forward<Args>(args)...);
    private_::logWrite(LogLevel::Notice, s);
}

/// Formats and writes a warning message when enabled.
template <typename... Args> inline void logw(fmt::format_string<Args...> fmtstr, Args &&...args) {
    auto s = fmt::format(fmtstr, std::forward<Args>(args)...);
    private_::logWrite(LogLevel::Warn, s);
}

/// Formats and writes an error message when enabled.
template <typename... Args> inline void loge(fmt::format_string<Args...> fmtstr, Args &&...args) {
    auto s = fmt::format(fmtstr, std::forward<Args>(args)...);
    private_::logWrite(LogLevel::Error, s);
}

// Unconditional single-stream output for command-line interfaces and other output that is part
// of a program's contract. Unlike the severity-based functions above, these functions are not
// affected by the configured log level and do not add prefixes
/// Formats and writes an unconditional message to standard output.
template <typename... Args>
inline void forceLog(fmt::format_string<Args...> fmtstr, Args &&...args) {
    auto s = fmt::format(fmtstr, std::forward<Args>(args)...);
    fmt::print(stdout, "{}", s);
}

/// Formats and writes an unconditional message to standard error.
template <typename... Args>
inline void forceLoge(fmt::format_string<Args...> fmtstr, Args &&...args) {
    auto s = fmt::format(fmtstr, std::forward<Args>(args)...);
    fmt::print(stderr, "{}", s);
}

// --- Optional runtime-format API ---
// Use this only when the format string is not a literal / not known at compile-time.
// Named differently to avoid overload ambiguity with string literals.
/// Formats a runtime format string and writes an informational message when enabled.
template <typename... Args> inline void logRuntime(fmt::string_view fmtstr, Args &&...args) {
    auto s = fmt::vformat(fmtstr, fmt::make_format_args(args...));
    private_::logWrite(LogLevel::Info, s);
}

/// Formats a runtime format string and writes a warning message when enabled.
template <typename... Args> inline void logwRuntime(fmt::string_view fmtstr, Args &&...args) {
    auto s = fmt::vformat(fmtstr, fmt::make_format_args(args...));
    private_::logWrite(LogLevel::Warn, s);
}

/// Formats a runtime format string and writes an error message when enabled.
template <typename... Args> inline void logeRuntime(fmt::string_view fmtstr, Args &&...args) {
    auto s = fmt::vformat(fmtstr, fmt::make_format_args(args...));
    private_::logWrite(LogLevel::Error, s);
}

} // namespace pek
