/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "Targets.h"

#include <cstdio>

namespace opk::log {

class ConsoleOutput final : public Target {
  public:
    ConsoleOutput(TargetType type, bool enabled, std::FILE *stream);

    void write(const Record &record) override;
    void flush() override;

  private:
    std::FILE *m_stream;
};

} // namespace opk::log
