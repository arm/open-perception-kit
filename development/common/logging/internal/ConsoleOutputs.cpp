/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ConsoleOutputs.h"

#include "Log.h"
#include "tools.h"

#include <cerrno>
#include <fmt/format.h>
#include <system_error>

namespace opk::log {

ConsoleOutput::ConsoleOutput(TargetType type, bool enabled, std::FILE *stream)
    : Target(type, enabled), m_stream(stream) {}

void ConsoleOutput::write(const Record &record) {
    switch (record.m_level) {
    case Level::Off:
        break;
    case Level::Info:
    case Level::Debug:
        fmt::print(m_stream, "{}", record.m_message);
        break;
    case Level::Notice:
        fmt::print(m_stream, "{}", tools::invert(record.m_message));
        break;
    case Level::Warn:
        fmt::print(m_stream, "W: {}", record.m_message);
        break;
    case Level::Error:
        fmt::print(m_stream, "E: {}", record.m_message);
        break;
    }
}

void ConsoleOutput::flush() {
    if (std::fflush(m_stream) == EOF) {
        throw std::system_error(errno, std::generic_category(), "failed to flush log target");
    }
}

} // namespace opk::log
