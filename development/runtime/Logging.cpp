/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 *************************************************************/

#include "runtime/Logging.h"

#include "Log.h"

#include <vector>

namespace {

opk::log::Level toLogLevel(opk::runtime::LogLevel level) {
    switch (level) {
    case opk::runtime::LogLevel::Off:
        return opk::log::Level::Off;
    case opk::runtime::LogLevel::Error:
        return opk::log::Level::Error;
    case opk::runtime::LogLevel::Warn:
        return opk::log::Level::Warn;
    case opk::runtime::LogLevel::Notice:
        return opk::log::Level::Notice;
    case opk::runtime::LogLevel::Info:
        return opk::log::Level::Info;
    case opk::runtime::LogLevel::Debug:
        return opk::log::Level::Debug;
    }
    return opk::log::Level::Info;
}

opk::runtime::LogLevel toRuntimeLogLevel(int level) {
    switch (level) {
    case static_cast<int>(opk::runtime::LogLevel::Off):
        return opk::runtime::LogLevel::Off;
    case static_cast<int>(opk::runtime::LogLevel::Error):
        return opk::runtime::LogLevel::Error;
    case static_cast<int>(opk::runtime::LogLevel::Warn):
        return opk::runtime::LogLevel::Warn;
    case static_cast<int>(opk::runtime::LogLevel::Notice):
        return opk::runtime::LogLevel::Notice;
    case static_cast<int>(opk::runtime::LogLevel::Debug):
        return opk::runtime::LogLevel::Debug;
    case static_cast<int>(opk::runtime::LogLevel::Info):
    default:
        return opk::runtime::LogLevel::Info;
    }
}

opk::log::TargetType toLogTarget(opk::runtime::LogTarget target) {
    switch (target) {
    case opk::runtime::LogTarget::Stdout:
        return opk::log::TargetType::Stdout;
    case opk::runtime::LogTarget::Stderr:
        return opk::log::TargetType::Stderr;
    case opk::runtime::LogTarget::File:
        return opk::log::TargetType::File;
    }
    return opk::log::TargetType::Stdout;
}

opk::runtime::LogTarget toRuntimeLogTarget(opk::log::TargetType target) {
    switch (target) {
    case opk::log::TargetType::Stdout:
        return opk::runtime::LogTarget::Stdout;
    case opk::log::TargetType::Stderr:
        return opk::runtime::LogTarget::Stderr;
    case opk::log::TargetType::File:
        return opk::runtime::LogTarget::File;
    }
    return opk::runtime::LogTarget::Stdout;
}

} // namespace

namespace opk::runtime {

LogLevel getLogLevel() {
    return toRuntimeLogLevel(opk::log::getLogLevel());
}

void setLogLevel(LogLevel logLevel) {
    opk::log::setLogLevel(static_cast<int>(toLogLevel(logLevel)));
}

void setLogLevel(int logLevel) {
    opk::log::setLogLevel(logLevel);
}

std::vector<LogTarget> getEnabledLogTargets() {
    const auto enabledTargets = opk::log::getEnabledLogTargets();
    std::vector<LogTarget> result;
    result.reserve(enabledTargets.size());
    for (const auto target : enabledTargets) {
        result.push_back(toRuntimeLogTarget(target));
    }
    return result;
}

bool setLogTargetState(LogTarget target, bool enabled) {
    return opk::log::setLogTargetState(toLogTarget(target), enabled);
}

void flushLog() {
    opk::log::flush();
}

} // namespace opk::runtime
