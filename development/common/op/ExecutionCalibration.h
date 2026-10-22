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

#include "perf/PipelineSample.h"

#include <optional>
#include <vector>

namespace opk::op {

enum class PreprocessPath { Cpu, SharedGpu, ReadbackGpu };

/** Compares allowed combinations using output measurements, without changing frame scheduling. */
class ExecutionCalibration {
  public:
    using Combination = std::vector<PreprocessPath>;
    explicit ExecutionCalibration(std::vector<std::vector<PreprocessPath>> choices);

    const Combination &selected() const;
    bool finished() const;
    bool measured() const;
    /** Ignore setup and queue warmup; observe only a fully prepared, working combination. */
    void observe(const perf::PipelineSample &sample,
                 const uint64_t inferredFrames,
                 const uint64_t inferenceCalls);

  private:
    struct Score {
        double videoFps;
        double inferenceFps;
        std::optional<double> cpuUsage;
        double latency;
    };

    void advance();
    static bool better(const Score &candidate, const Score &previous);

    std::vector<std::vector<PreprocessPath>> choices;
    std::vector<size_t> indices;
    Combination current;
    Combination best;
    std::optional<Score> bestScore;
    std::optional<perf::PipelineSample> warmup;
    std::optional<perf::PipelineSample> window;
    uint64_t initialInferredFrames{0};
    uint64_t initialInferenceCalls{0};
    bool complete{false};
};

} // namespace opk::op
