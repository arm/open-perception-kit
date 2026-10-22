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

#include "op/ExecutionCalibration.h"

#include "Log.h"

#include <algorithm>
#include <stdexcept>

namespace opk::op {
namespace {
constexpr double warmupSeconds{0.5};
constexpr double minimumWindowSeconds{2.0};
constexpr double maximumWindowSeconds{10.0};
constexpr uint64_t minimumInferenceFrames{3};
constexpr double throughputTargetFps{30.0};
constexpr double throughputTolerance{0.03};
constexpr double latencyTolerance{0.10};
constexpr double cpuUsageTolerance{0.03};
constexpr double millisecondsPerSecond{1000.0};

} // namespace

ExecutionCalibration::ExecutionCalibration(std::vector<std::vector<PreprocessPath>> options)
    : choices{std::move(options)}, indices(choices.size(), 0) {
    for (const auto &option : choices) {
        if (option.empty())
            throw std::invalid_argument("Calibration needs at least one allowed path per stage");
        current.push_back(option.front());
    }
    best = current;
    complete = choices.empty();
}

const ExecutionCalibration::Combination &ExecutionCalibration::selected() const {
    return complete ? best : current;
}

bool ExecutionCalibration::finished() const {
    return complete;
}
bool ExecutionCalibration::measured() const {
    return bestScore.has_value();
}

bool ExecutionCalibration::better(const Score &candidate, const Score &previous) {
    const double candidateRate{std::min(candidate.inferenceFps, throughputTargetFps)};
    const double previousRate{std::min(previous.inferenceFps, throughputTargetFps)};
    if (candidate.videoFps < previous.videoFps * (1.0 - throughputTolerance))
        return false;
    if (candidateRate > previousRate * (1.0 + throughputTolerance))
        return true;
    if (candidateRate < previousRate * (1.0 - throughputTolerance))
        return false;
    if (std::min(candidate.videoFps, throughputTargetFps) >
        std::min(previous.videoFps, throughputTargetFps) * (1.0 + throughputTolerance))
        return true;
    if (candidate.latencySeconds > previous.latencySeconds * (1.0 + latencyTolerance))
        return false;
    const bool lowerCpu{candidate.cpuUsage && previous.cpuUsage &&
                        *candidate.cpuUsage < *previous.cpuUsage * (1.0 - cpuUsageTolerance)};
    return lowerCpu ||
           candidate.latencySeconds < previous.latencySeconds * (1.0 - latencyTolerance);
}

void ExecutionCalibration::observe(const perf::PipelineSample &sample) {
    if (complete || sample.endpoint.empty())
        return;
    if (!warmup) {
        warmup = sample;
        return;
    }
    const auto interval{perf::calculatePipelineIntervalMetrics(window ? *window : *warmup, sample)};
    if (!interval) {
        warmup = sample;
        window.reset();
        return;
    }
    if (!window) {
        if (interval->elapsedSeconds >= warmupSeconds && interval->matchedFrames != 0)
            window = sample;
        return;
    }
    const auto &score{*interval};
    if (score.elapsedSeconds < minimumWindowSeconds)
        return;
    if (score.inferredFrames < minimumInferenceFrames || score.outputFrames == 0 ||
        score.matchedFrames == 0) {
        if (score.elapsedSeconds >= maximumWindowSeconds)
            advance();
        return;
    }
    log::info("Preprocessing calibration: output {:.2f} fps, inference {:.2f} fps, "
              "input-to-output {:.2f} ms, CPU {}, {} inferred frames, {} inference "
              "calls, {} skips.\n",
              score.videoFps,
              score.inferenceFps,
              score.latencySeconds * millisecondsPerSecond,
              score.cpuUsage ? fmt::format("{:.2f} cores", *score.cpuUsage) : "unavailable",
              score.inferredFrames,
              score.inferenceCalls,
              score.skippedFrames);
    if (!bestScore || better(score, *bestScore)) {
        best = current;
        bestScore = score;
    }
    advance();
}

void ExecutionCalibration::advance() {
    warmup.reset();
    window.reset();
    for (size_t index{indices.size()}; index > 0; --index) {
        const size_t stage{index - 1};
        ++indices[stage];
        if (indices[stage] < choices[stage].size()) {
            current[stage] = choices[stage][indices[stage]];
            return;
        }
        indices[stage] = 0;
        current[stage] = choices[stage].front();
    }
    complete = true;
}

} // namespace opk::op
