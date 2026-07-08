/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "pek/Log.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>

namespace {

std::atomic<int> &access_log_level() {
    // OPK_LOG_LEVEL supplies the initial process configuration. Runtime updates modify only this
    // internal value and never mutate the process environment.
    static std::atomic<int> logLevel = [] {
        const char *value = std::getenv("OPK_LOG_LEVEL"); // NOLINT(concurrency-mt-unsafe)
        if (value == nullptr) {
            return pek::log_level_value(pek::defaultLogLevel);
        }
        return pek::parse_log_level(value).value_or(pek::log_level_value(pek::defaultLogLevel));
    }();
    return logLevel;
}

} // namespace

std::optional<int> pek::parse_log_level(std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }

    int parsedLevel = 0;
    for (char c : value) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        parsedLevel = std::min((parsedLevel * 10) + (c - '0'), log_level_value(LogLevel::Info));
    }
    return parsedLevel;
}

int pek::current_log_level() {
    return access_log_level().load(std::memory_order_relaxed);
}

void pek::set_log_level(int logLevel) {
    access_log_level().store(
        std::clamp(logLevel, log_level_value(LogLevel::Off), log_level_value(LogLevel::Info)),
        std::memory_order_relaxed);
}

void pek::detail::write_stdout(fmt::string_view msg) {
    fmt::print(stdout, "{}", msg);
}

void pek::detail::write_stderr(fmt::string_view msg) {
    fmt::print(stderr, "{}", msg);
}

void pek::log_write(LogLevel lvl, fmt::string_view msg) {
    if (!should_log(lvl)) {
        return;
    }

    switch (lvl) {
    case LogLevel::Off:
        break;
    case LogLevel::Info:
        detail::write_stdout(msg);
        break;
    case LogLevel::Notice:
        detail::write_stdout(LogTools::invert(msg.data()));
        break;
    case LogLevel::Warn: {
        const auto output = fmt::format("W: {}", msg);
        detail::write_stdout(output);
        detail::write_stderr(output);
        break;
    }
    case LogLevel::Error: {
        const auto output = fmt::format("E: {}", msg);
        detail::write_stdout(output);
        detail::write_stderr(output);
        break;
    }
    }
}
