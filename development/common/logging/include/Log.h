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

namespace pek {

// These functions are only meant to be used internally inside the logging component.
namespace private_ {

void logWrite(LogLevel level, std::string &&message);

} // namespace private_

int getLogLevel();
void setLogLevel(int logLevel);
std::vector<LogTargetType> getEnabledLogTargets();
bool setLogTargetState(LogTargetType output, bool enabled);
void logFlush();

template <typename... Args> inline void log(fmt::format_string<Args...> format, Args &&...args) {
    auto message = fmt::format(format, std::forward<Args>(args)...);
    private_::logWrite(LogLevel::Info, std::move(message));
}

template <typename... Args> inline void logn(fmt::format_string<Args...> format, Args &&...args) {
    auto message = fmt::format(format, std::forward<Args>(args)...);
    private_::logWrite(LogLevel::Notice, std::move(message));
}

template <typename... Args> inline void logw(fmt::format_string<Args...> format, Args &&...args) {
    auto message = fmt::format(format, std::forward<Args>(args)...);
    private_::logWrite(LogLevel::Warn, std::move(message));
}

template <typename... Args> inline void loge(fmt::format_string<Args...> format, Args &&...args) {
    auto message = fmt::format(format, std::forward<Args>(args)...);
    private_::logWrite(LogLevel::Error, std::move(message));
}

template <typename... Args>
inline void forceLog(fmt::format_string<Args...> format, Args &&...args) {
    auto message = fmt::format(format, std::forward<Args>(args)...);
    fmt::print(stdout, "{}", message);
}

template <typename... Args>
inline void forceLoge(fmt::format_string<Args...> format, Args &&...args) {
    auto message = fmt::format(format, std::forward<Args>(args)...);
    fmt::print(stderr, "{}", message);
}

template <typename... Args> inline void logRuntime(fmt::string_view format, Args &&...args) {
    auto message = fmt::vformat(format, fmt::make_format_args(args...));
    private_::logWrite(LogLevel::Info, std::move(message));
}

template <typename... Args> inline void logwRuntime(fmt::string_view format, Args &&...args) {
    auto message = fmt::vformat(format, fmt::make_format_args(args...));
    private_::logWrite(LogLevel::Warn, std::move(message));
}

template <typename... Args> inline void logeRuntime(fmt::string_view format, Args &&...args) {
    auto message = fmt::vformat(format, fmt::make_format_args(args...));
    private_::logWrite(LogLevel::Error, std::move(message));
}

} // namespace pek
