/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "LogTargets.h"

#include <cstdio>

namespace pek::logging {

class ConsoleOutput final : public LogTarget {
  public:
    ConsoleOutput(LogTargetType type, bool enabled, std::FILE *stream);

    void write(const LogRecord &record) override;
    void flush() override;

  private:
    std::FILE *m_stream;
};

} // namespace pek::logging
