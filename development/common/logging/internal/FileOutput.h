/*************************************************************
 * Copyright (C) 2026 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "Targets.h"

#include <fstream>
#include <optional>
#include <string>

namespace opk::log {

class FileOutput final : public Target {
  public:
    FileOutput(std::string fileName, bool enabled);

    void setEnabled(bool enabled) noexcept override;
    void write(const Record &record) override;
    void flush() override;

  private:
    std::string m_fileName;
    std::optional<std::ofstream> m_stream;
};

} // namespace opk::log
