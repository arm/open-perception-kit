/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "LogTypes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pek::log {

struct LogRecord {
    LogLevel m_level{LogLevel::Off};
    std::string m_message;
    std::uint64_t m_sequence{0};
};

class LogTarget {
  public:
    virtual ~LogTarget() = default;

    LogTargetType getType() const {
        return m_type;
    }

    bool isEnabled() const {
        return m_enabled;
    }

    void setEnabled(bool enabled) {
        m_enabled = enabled;
    }

    virtual void write(const LogRecord &record) = 0;
    virtual void flush() = 0;

  protected:
    LogTarget(LogTargetType type, bool enabled) : m_type(type), m_enabled(enabled) {}

  private:
    LogTargetType m_type;
    bool m_enabled;
};

using LogTargets = std::vector<std::unique_ptr<LogTarget>>;

LogTargets createBuiltInLogTargets(const std::vector<LogTargetType> &enabledTargets);

} // namespace pek::log
