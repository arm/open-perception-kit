/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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
