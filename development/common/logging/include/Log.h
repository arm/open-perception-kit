/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "LogTypes.h"

#include <cstdio>
#include <fmt/format.h>
#include <string>
#include <utility>
#include <vector>

namespace pek::log {

// These functions are only meant to be used internally inside the logging component.
namespace private_ {

void write(Level level, std::string &&message);

} // namespace private_

int getLogLevel();
void setLogLevel(int logLevel);
std::vector<TargetType> getEnabledLogTargets();
bool setLogTargetState(TargetType output, bool enabled);
void flush();

template <typename... Args> inline void info(fmt::format_string<Args...> format, Args &&...args) {
    auto message = fmt::format(format, std::forward<Args>(args)...);
    private_::write(Level::Info, std::move(message));
}

template <typename... Args> inline void notice(fmt::format_string<Args...> format, Args &&...args) {
    auto message = fmt::format(format, std::forward<Args>(args)...);
    private_::write(Level::Notice, std::move(message));
}

template <typename... Args>
inline void warning(fmt::format_string<Args...> format, Args &&...args) {
    auto message = fmt::format(format, std::forward<Args>(args)...);
    private_::write(Level::Warn, std::move(message));
}

template <typename... Args> inline void error(fmt::format_string<Args...> format, Args &&...args) {
    auto message = fmt::format(format, std::forward<Args>(args)...);
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

template <typename... Args> inline void infoRuntime(fmt::string_view format, Args &&...args) {
    auto message = fmt::vformat(format, fmt::make_format_args(args...));
    private_::write(Level::Info, std::move(message));
}

template <typename... Args> inline void warningRuntime(fmt::string_view format, Args &&...args) {
    auto message = fmt::vformat(format, fmt::make_format_args(args...));
    private_::write(Level::Warn, std::move(message));
}

template <typename... Args> inline void errorRuntime(fmt::string_view format, Args &&...args) {
    auto message = fmt::vformat(format, fmt::make_format_args(args...));
    private_::write(Level::Error, std::move(message));
}

} // namespace pek::log
