/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

#ifndef __STATUS_REPORTER_H__
#define __STATUS_REPORTER_H__

#include <functional>
#include <nlohmann/json.hpp>

class StatusReporter {
  protected:
    friend class CtrlWebSocket;

    // shall be called when the reporter has something to report
    // by default it does nothing
    // gets its real value when this reporter gets registered to CtrlWebSocket
    std::function<void()> trigger_reporting = []() {};

  public:
    virtual nlohmann::json report() const = 0;
};

#endif // !__STATUS_REPORTER_H__
