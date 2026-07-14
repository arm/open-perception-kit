/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "pek/Log.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>

namespace pek::private_ {

std::atomic<int> &accessLogLevel() {
    // OPK_LOG_LEVEL supplies the initial process configuration. Runtime updates modify only this
    // internal value and never mutate the process environment.
    static std::atomic<int> logLevel = [] {
        const char *value = std::getenv("OPK_LOG_LEVEL"); // NOLINT(concurrency-mt-unsafe)
        if (value == nullptr) {
            return logLevelValue(defaultLogLevel);
        }
        return parseLogLevel(value).value_or(logLevelValue(defaultLogLevel));
    }();
    return logLevel;
}

std::optional<int> parseLogLevel(std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }

    int parsedLevel = 0;
    for (char c : value) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        parsedLevel = std::min((parsedLevel * 10) + (c - '0'), logLevelValue(LogLevel::Info));
    }
    return parsedLevel;
}

bool shouldLog(LogLevel lvl) {
    return lvl != LogLevel::Off && getLogLevel() >= logLevelValue(lvl);
}

void logWrite(LogLevel lvl, fmt::string_view msg) {
    if (!shouldLog(lvl)) {
        return;
    }

    switch (lvl) {
    case LogLevel::Off:
        break;
    case LogLevel::Info:
        fmt::print(stdout, "{}", msg);
        break;
    case LogLevel::Notice:
        fmt::print(stdout, "{}", invert(msg.data()));
        break;
    case LogLevel::Warn: {
        const auto output = fmt::format("W: {}", msg);
        fmt::print(stdout, "{}", output);
        fmt::print(stderr, "{}", output);
        break;
    }
    case LogLevel::Error: {
        const auto output = fmt::format("E: {}", msg);
        fmt::print(stdout, "{}", output);
        fmt::print(stderr, "{}", output);
        break;
    }
    }
}

} // namespace pek::private_

int pek::getLogLevel() {
    return private_::accessLogLevel().load(std::memory_order_relaxed);
}

void pek::setLogLevel(int logLevel) {
    private_::accessLogLevel().store(std::clamp(logLevel,
                                                private_::logLevelValue(LogLevel::Off),
                                                private_::logLevelValue(LogLevel::Info)),
                                     std::memory_order_relaxed);
}
