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

#include <cstdint>
#include <string>

namespace opk::perf {

/** Cumulative counters; compare snapshots only when generation and endpoint match. */
struct PipelineSample {
    uint64_t generation{0};
    uint64_t inputFrames{0};
    uint64_t outputFrames{0};
    uint64_t matchedFrames{0};
    double latencySeconds{0};
    double steadySeconds{0};
    double processCpuSeconds{0};
    bool processCpuAvailable{false};
    uint64_t renderedFrames{0};
    uint64_t droppedFrames{0};
    bool sinkStatsAvailable{false};
    std::string endpoint;
    std::string configuration;
};

} // namespace opk::perf
