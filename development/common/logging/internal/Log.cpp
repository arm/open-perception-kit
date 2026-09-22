/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "Log.h"

#include "Logger.h"
#include "Targets.h"

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

namespace opk::log {

namespace { // unnamed namespace to keep functions local

constexpr int levelValue(Level level) {
    return static_cast<int>(level);
}

std::optional<int> parseLogLevel(std::string_view value) {
    if (value.size() != 1 || value.front() < '0' || value.front() > '9') {
        return std::nullopt;
    }
    return std::min(value.front() - '0', levelValue(Level::Debug));
}

bool shouldLog(Level level) {
    return level != Level::Off && getLogLevel() >= levelValue(level);
}

struct InitialLogConfiguration {
    int level;
    std::vector<TargetType> enabledTargets;
    std::string fileName;
};

std::optional<int> configuredLogLevel() {
    const char *value = std::getenv("OPK_LOG_LEVEL"); // NOLINT(concurrency-mt-unsafe)
    if (value == nullptr) {
        return std::nullopt;
    }
    return parseLogLevel(value);
}

std::vector<TargetType> configuredLogTargets() {
    const char *value = std::getenv("OPK_LOG_TARGETS"); // NOLINT(concurrency-mt-unsafe)
    if (value == nullptr) {
        return {TargetType::Stdout};
    }

    const std::string_view configuredTargets(value);
    if (configuredTargets == "none") {
        return {};
    }

    std::vector<TargetType> targets;
    // A small lambda to append the detected log target to the targets list if it is not already
    // present.
    const auto appendIfMissing = [&targets](TargetType target) {
        if (std::find(targets.begin(), targets.end(), target) == targets.end()) {
            targets.push_back(target);
        }
    };

    std::istringstream targetStream{std::string(configuredTargets)};
    for (std::string token; std::getline(targetStream, token, ',');) {
        if (token == "stdout") {
            appendIfMissing(TargetType::Stdout);
        } else if (token == "stderr") {
            appendIfMissing(TargetType::Stderr);
        } else if (token == "file") {
            appendIfMissing(TargetType::File);
        }
    }

    if (targets.empty()) {
        targets.push_back(TargetType::Stdout);
    }
    return targets;
}

std::string configuredLogFile() {
    const char *value = std::getenv("OPK_LOG_FILE"); // NOLINT(concurrency-mt-unsafe)
    return value == nullptr || *value == '\0' ? "opk.log" : value;
}

InitialLogConfiguration &initialLogConfiguration() {
    static InitialLogConfiguration configuration = [] {
        const int level = configuredLogLevel().value_or(levelValue(defaultLogLevel));
        auto targets = configuredLogTargets();
        return InitialLogConfiguration{level, std::move(targets), configuredLogFile()};
    }();
    return configuration;
}

std::atomic<int> &accessLogLevel() {
    static std::atomic<int> logLevel{initialLogConfiguration().level};
    return logLevel;
}

struct LoggerState {
    inline static std::atomic<Logger *> initializedLogger{nullptr};
};

Logger &processLogger() {
    static Logger logger(createBuiltInLogTargets(initialLogConfiguration().enabledTargets,
                                                 initialLogConfiguration().fileName));
    LoggerState::initializedLogger.store(&logger);
    return logger;
}

} // namespace

namespace private_ {

void write(Level level, std::string &&message) {
    if (shouldLog(level)) {
        processLogger().write(level, std::move(message));
    }
}

} // namespace private_

int getLogLevel() {
    return accessLogLevel().load(std::memory_order_relaxed);
}

void setLogLevel(int logLevel) {
    accessLogLevel().store(std::clamp(logLevel, levelValue(Level::Off), levelValue(Level::Debug)),
                           std::memory_order_relaxed);
}

std::vector<TargetType> getEnabledLogTargets() {
    return processLogger().getEnabledTargets();
}

bool setLogTargetState(TargetType output, bool enabled) {
    return processLogger().setTargetState(output, enabled);
}

void flush() {
    if (auto *logger = LoggerState::initializedLogger.load()) {
        logger->flush();
    }
}

} // namespace opk::log
