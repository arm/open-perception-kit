/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "ConsoleOutputs.h"

#include "Log.h"
#include "LogTools.h"

#include <cerrno>
#include <fmt/format.h>
#include <system_error>

namespace pek::log {

ConsoleOutput::ConsoleOutput(LogTargetType type, bool enabled, std::FILE *stream)
    : LogTarget(type, enabled), m_stream(stream) {}

void ConsoleOutput::write(const LogRecord &record) {
    switch (record.m_level) {
    case LogLevel::Off:
        break;
    case LogLevel::Info:
        fmt::print(m_stream, "{}", record.m_message);
        break;
    case LogLevel::Notice:
        fmt::print(m_stream, "{}", LogTools::invert(record.m_message));
        break;
    case LogLevel::Warn:
        fmt::print(m_stream, "W: {}", record.m_message);
        break;
    case LogLevel::Error:
        fmt::print(m_stream, "E: {}", record.m_message);
        break;
    }
}

void ConsoleOutput::flush() {
    if (std::fflush(m_stream) == EOF) {
        throw std::system_error(errno, std::generic_category(), "failed to flush log target");
    }
}

} // namespace pek::log
