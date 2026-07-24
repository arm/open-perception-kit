/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#include "FileOutput.h"

#include <ios>
#include <utility>

namespace pek::log {

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

} // namespace pek::log
