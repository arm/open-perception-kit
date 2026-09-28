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
