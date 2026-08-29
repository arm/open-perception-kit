/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "runtime/Logging.h"

#include "Log.h"

#include <vector>

namespace {

pek::log::Level toLogLevel(pek::runtime::LogLevel level) {
    switch (level) {
    case pek::runtime::LogLevel::Off:
        return pek::log::Level::Off;
    case pek::runtime::LogLevel::Error:
        return pek::log::Level::Error;
    case pek::runtime::LogLevel::Warn:
        return pek::log::Level::Warn;
    case pek::runtime::LogLevel::Notice:
        return pek::log::Level::Notice;
    case pek::runtime::LogLevel::Info:
        return pek::log::Level::Info;
    case pek::runtime::LogLevel::Debug:
        return pek::log::Level::Debug;
    }
    return pek::log::Level::Info;
}

pek::runtime::LogLevel toRuntimeLogLevel(int level) {
    switch (level) {
    case static_cast<int>(pek::runtime::LogLevel::Off):
        return pek::runtime::LogLevel::Off;
    case static_cast<int>(pek::runtime::LogLevel::Error):
        return pek::runtime::LogLevel::Error;
    case static_cast<int>(pek::runtime::LogLevel::Warn):
        return pek::runtime::LogLevel::Warn;
    case static_cast<int>(pek::runtime::LogLevel::Notice):
        return pek::runtime::LogLevel::Notice;
    case static_cast<int>(pek::runtime::LogLevel::Debug):
        return pek::runtime::LogLevel::Debug;
    case static_cast<int>(pek::runtime::LogLevel::Info):
    default:
        return pek::runtime::LogLevel::Info;
    }
}

pek::log::TargetType toLogTarget(pek::runtime::LogTarget target) {
    switch (target) {
    case pek::runtime::LogTarget::Stdout:
        return pek::log::TargetType::Stdout;
    case pek::runtime::LogTarget::Stderr:
        return pek::log::TargetType::Stderr;
    case pek::runtime::LogTarget::File:
        return pek::log::TargetType::File;
    }
    return pek::log::TargetType::Stdout;
}

pek::runtime::LogTarget toRuntimeLogTarget(pek::log::TargetType target) {
    switch (target) {
    case pek::log::TargetType::Stdout:
        return pek::runtime::LogTarget::Stdout;
    case pek::log::TargetType::Stderr:
        return pek::runtime::LogTarget::Stderr;
    case pek::log::TargetType::File:
        return pek::runtime::LogTarget::File;
    }
    return pek::runtime::LogTarget::Stdout;
}

} // namespace

namespace pek::runtime {

LogLevel getLogLevel() {
    return toRuntimeLogLevel(pek::log::getLogLevel());
}

void setLogLevel(LogLevel logLevel) {
    pek::log::setLogLevel(static_cast<int>(toLogLevel(logLevel)));
}

void setLogLevel(int logLevel) {
    pek::log::setLogLevel(logLevel);
}

std::vector<LogTarget> getEnabledLogTargets() {
    const auto enabledTargets = pek::log::getEnabledLogTargets();
    std::vector<LogTarget> result;
    result.reserve(enabledTargets.size());
    for (const auto target : enabledTargets) {
        result.push_back(toRuntimeLogTarget(target));
    }
    return result;
}

bool setLogTargetState(LogTarget target, bool enabled) {
    return pek::log::setLogTargetState(toLogTarget(target), enabled);
}

void flushLog() {
    pek::log::flush();
}

} // namespace pek::runtime
