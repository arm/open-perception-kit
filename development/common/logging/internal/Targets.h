/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "LogTypes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace opk::log {

struct Record {
    Level m_level{Level::Off};
    std::string m_message;
    std::uint64_t m_sequence{0};
};

class Target {
  public:
    virtual ~Target() = default;

    TargetType getType() const {
        return m_type;
    }

    bool isEnabled() const {
        return m_enabled;
    }

    virtual void setEnabled(bool enabled) noexcept {
        m_enabled = enabled;
    }

    virtual void write(const Record &record) = 0;
    virtual void flush() = 0;

  protected:
    Target(TargetType type, bool enabled) : m_type(type), m_enabled(enabled) {}

  private:
    TargetType m_type;
    bool m_enabled;
};

using Targets = std::vector<std::unique_ptr<Target>>;

Targets createBuiltInLogTargets(const std::vector<TargetType> &enabledTargets,
                                const std::string &logFileName);

} // namespace opk::log
