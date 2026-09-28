/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 *************************************************************/

#pragma once

#include "LogTypes.h"

#include <cstdio>
#include <fmt/format.h>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace opk::log {

// Escape a string for diagnostics outside the OPK logger, such as GStreamer logs.
std::string escape(std::string_view text);

// These functions are only meant to be used internally inside the logging component.
namespace private_ {

template <typename T> decltype(auto) escapeArgument(T &&value) {
    if constexpr (std::is_convertible_v<T, fmt::string_view>) {
        const fmt::string_view text(value);
        return escape({text.data(), text.size()});
    } else {
        return std::forward<T>(value);
    }
}

template <typename... Args>
std::string formatMessage(fmt::format_string<Args...> format, Args &&...args) {
    return fmt::format(fmt::runtime(format), escapeArgument(std::forward<Args>(args))...);
}

template <typename... Args> struct FormatWithLocation {
    fmt::format_string<Args...> value;
    std::source_location location;

    template <typename String>
    consteval FormatWithLocation(const String &format,
                                 std::source_location caller = std::source_location::current())
        : value(format), location(caller) {}
};

void write(Level level, std::string &&message);

} // namespace private_

int getLogLevel();
void setLogLevel(int logLevel);
std::vector<TargetType> getEnabledLogTargets();
bool setLogTargetState(TargetType output, bool enabled);
// Flush an existing logger; do not initialize logging just to flush it.
void flush();

template <typename... Args>
inline void debug(private_::FormatWithLocation<std::type_identity_t<Args>...> format,
                  Args &&...args) {
    const std::string_view fileName(format.location.file_name());
    const auto separator = fileName.find_last_of("/\\");
    const auto basename = fileName.substr(separator == std::string_view::npos ? 0 : separator + 1);
    auto message = fmt::format("{}[{}:{}] {}{}\n",
                               color::BrightCyan,
                               basename,
                               format.location.line(),
                               color::ResetColor,
                               private_::formatMessage(format.value, std::forward<Args>(args)...));
    private_::write(Level::Debug, std::move(message));
}

template <typename... Args> inline void info(fmt::format_string<Args...> format, Args &&...args) {
    auto message = private_::formatMessage(format, std::forward<Args>(args)...);
    private_::write(Level::Info, std::move(message));
}

template <typename... Args> inline void notice(fmt::format_string<Args...> format, Args &&...args) {
    auto message = private_::formatMessage(format, std::forward<Args>(args)...);
    private_::write(Level::Notice, std::move(message));
}

template <typename... Args>
inline void warning(fmt::format_string<Args...> format, Args &&...args) {
    auto message = private_::formatMessage(format, std::forward<Args>(args)...);
    private_::write(Level::Warn, std::move(message));
}

template <typename... Args> inline void error(fmt::format_string<Args...> format, Args &&...args) {
    auto message = private_::formatMessage(format, std::forward<Args>(args)...);
    private_::write(Level::Error, std::move(message));
}

template <typename... Args>
inline void instantInfo(fmt::format_string<Args...> format, Args &&...args) {
    auto message = fmt::format(format, std::forward<Args>(args)...);
    fmt::print(stdout, "{}", message);
}

template <typename... Args>
inline void instantError(fmt::format_string<Args...> format, Args &&...args) {
    auto message = fmt::format(format, std::forward<Args>(args)...);
    fmt::print(stderr, "{}", message);
}

} // namespace opk::log
