/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "Log.h"

#include "LogTargets.h"
#include "Logger.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pek {

namespace { // unnamed namespace to keep functions local

constexpr int logLevelValue(LogLevel level) {
    return static_cast<int>(level);
}

std::optional<int> parseLogLevel(std::string_view value) {
    if (value.size() != 1 || value.front() < '0' || value.front() > '9') {
        return std::nullopt;
    }
    return std::min(value.front() - '0', logLevelValue(LogLevel::Info));
}

bool shouldLog(LogLevel level) {
    return level != LogLevel::Off && getLogLevel() >= logLevelValue(level);
}

struct InitialLogConfiguration {
    int level;
    std::vector<LogTargetType> enabledTargets;
};

std::optional<int> configuredLogLevel() {
    const char *value = std::getenv("OPK_LOG_LEVEL"); // NOLINT(concurrency-mt-unsafe)
    if (value == nullptr) {
        forceLoge("OPK_LOG_LEVEL is not set; defaulting to 4 (Info).\n");
        return std::nullopt;
    }
    return parseLogLevel(value);
}

std::vector<LogTargetType> configuredLogTargets() {
    const char *value = std::getenv("OPK_LOG_TARGETS"); // NOLINT(concurrency-mt-unsafe)
    if (value == nullptr) {
        forceLoge("OPK_LOG_TARGETS is not set; defaulting to stdout.\n");
        return {LogTargetType::Stdout};
    }

    const std::string_view configuredTargets(value);
    if (configuredTargets == "none") {
        return {};
    }

    std::vector<LogTargetType> targets;
    // A small lambda to append the detected log target to the targets list if it is not already
    // present.
    const auto appendIfMissing = [&targets](LogTargetType target) {
        if (std::find(targets.begin(), targets.end(), target) == targets.end()) {
            targets.push_back(target);
        }
    };

    std::istringstream targetStream{std::string(configuredTargets)};
    for (std::string token; std::getline(targetStream, token, ',');) {
        if (token == "stdout") {
            appendIfMissing(LogTargetType::Stdout);
        } else if (token == "stderr") {
            appendIfMissing(LogTargetType::Stderr);
        }
    }

    if (targets.empty()) {
        targets.push_back(LogTargetType::Stdout);
    }
    return targets;
}

InitialLogConfiguration &initialLogConfiguration() {
    static InitialLogConfiguration configuration = [] {
        const int level = configuredLogLevel().value_or(logLevelValue(defaultLogLevel));
        auto targets = configuredLogTargets();
        return InitialLogConfiguration{level, std::move(targets)};
    }();
    return configuration;
}

std::atomic<int> &accessLogLevel() {
    static std::atomic<int> logLevel{initialLogConfiguration().level};
    return logLevel;
}

logging::Logger &processLogger() {
    static logging::Logger logger(
        logging::createBuiltInLogTargets(initialLogConfiguration().enabledTargets));
    return logger;
}

} // namespace

namespace private_ {

void logWrite(LogLevel level, std::string &&message) {
    if (shouldLog(level)) {
        processLogger().write(level, std::move(message));
    }
}

} // namespace private_

int getLogLevel() {
    return accessLogLevel().load(std::memory_order_relaxed);
}

void setLogLevel(int logLevel) {
    accessLogLevel().store(
        std::clamp(logLevel, logLevelValue(LogLevel::Off), logLevelValue(LogLevel::Info)),
        std::memory_order_relaxed);
}

std::vector<LogTargetType> getEnabledLogTargets() {
    return processLogger().getEnabledTargets();
}

bool setLogTargetState(LogTargetType output, bool enabled) {
    return processLogger().setTargetState(output, enabled);
}

void logFlush() {
    processLogger().flush();
}

} // namespace pek
