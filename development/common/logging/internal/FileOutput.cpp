/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

#include "FileOutput.h"

#include <ios>
#include <utility>

namespace opk::log {

FileOutput::FileOutput(std::string fileName, bool enabled)
    : Target(TargetType::File, enabled), m_fileName(std::move(fileName)) {}

void FileOutput::setEnabled(bool enabled) noexcept {
    Target::setEnabled(enabled);
    if (!enabled) {
        m_stream.reset(); // Reset not only resets the optional, but also calls the destructor of
                          // the stored variable, which closes the file stream.
    }
}

void FileOutput::write(const Record &record) {
    if (!m_stream) {
        m_stream.emplace();
        m_stream->exceptions(std::ios::failbit | std::ios::badbit);
        m_stream->open(m_fileName, std::ios::out | std::ios::app | std::ios::binary);
    }
    *m_stream << record.m_message;
}

void FileOutput::flush() {
    if (m_stream) {
        m_stream->flush();
    }
}

} // namespace opk::log
