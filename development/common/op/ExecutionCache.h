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

#include "op/ExecutionCalibration.h"

#include <functional>
#include <future>
#include <set>
#include <string>

namespace opk::op {

/** User-local performance hints; one background writer preserves save/discard ordering. */
class ExecutionCache {
  public:
    ~ExecutionCache();
    std::optional<ExecutionCalibration::Combination> load(const std::string &setup,
                                                          const size_t stages) const;
    void save(const std::string &setup, const ExecutionCalibration::Combination &selection);
    void discard(const std::string &setup);
    /** Nonblocking; start queued I/O only after the previous writer has completed. */
    void poll();

  private:
    std::future<void> writer;
    std::vector<std::function<void()>> pending;
    std::set<std::string> invalidated;
};

} // namespace opk::op
