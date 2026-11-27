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

#include "postproc/PitchEstimationParser.h"
#include "opk/AudioResults.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fmt/core.h>
#include <limits>

namespace opk::stdop::postproc {
namespace {
auto invalid(const char *const message) {
    return tl::unexpected(OPK_ERROR(opk::ErrorFlag::InvalidData, message));
}
struct Configuration {
    size_t tensorIndex, binCount, radius;
    bool sigmoid, weighted;
    double centsOffset, centsPerBin, referenceHz, minHz, maxHz, threshold, minimumRms;
    bool operator==(const Configuration &) const = default;
};
opk::Result<Configuration> configure(const opk::AttributeMap &a) {
    for (const auto *const key : {"outputTensorIndex", "binCount", "weightedRadius"})
        if (!a.contains(key) || !a.at(key).isInt())
            return invalid("Pitch parser requires integer tensor index, bin count and radius");
    const auto index = a.getInt("outputTensorIndex");
    const auto bins = a.getInt("binCount");
    const auto radius = a.getInt("weightedRadius");
    if (index < 0 || static_cast<size_t>(index) >= opk::MaxTensorCount || bins < 1 ||
        bins > 1048576 || radius < 0 || radius > 1048576)
        return invalid("Pitch parser integer configuration is out of range");
    for (const auto *const key : {"centsOffset",
                                  "centsPerBin",
                                  "referenceFrequencyHz",
                                  "minFrequencyHz",
                                  "maxFrequencyHz",
                                  "periodicityThreshold",
                                  "minimumRms"})
        if (!a.contains(key) || (!a.at(key).isDouble() && !a.at(key).isInt()) ||
            !std::isfinite(a.getDouble(key)))
            return invalid("Pitch parser requires finite numeric configuration");
    for (const auto *const key : {"activation", "decoder"})
        if (!a.contains(key) || !a.at(key).isString())
            return invalid("Pitch parser requires activation and decoder strings");
    const auto &activation = a.getString("activation");
    const auto &decoder = a.getString("decoder");
    if ((activation != "none" && activation != "sigmoid") ||
        (decoder != "argmax" && decoder != "weightedArgmax"))
        return invalid("Pitch parser activation or decoder is unsupported");
    const Configuration c{.tensorIndex = static_cast<size_t>(index),
                          .binCount = static_cast<size_t>(bins),
                          .radius = static_cast<size_t>(radius),
                          .sigmoid = activation == "sigmoid",
                          .weighted = decoder == "weightedArgmax",
                          .centsOffset = a.getDouble("centsOffset"),
                          .centsPerBin = a.getDouble("centsPerBin"),
                          .referenceHz = a.getDouble("referenceFrequencyHz"),
                          .minHz = a.getDouble("minFrequencyHz"),
                          .maxHz = a.getDouble("maxFrequencyHz"),
                          .threshold = a.getDouble("periodicityThreshold"),
                          .minimumRms = a.getDouble("minimumRms")};
    if (c.centsPerBin <= 0 || c.referenceHz <= 0 || c.minHz <= 0 || c.maxHz < c.minHz ||
        c.maxHz > std::numeric_limits<float>::max() || c.threshold < 0 || c.threshold > 1 ||
        c.minimumRms < 0)
        return invalid("Pitch parser grid, frequency limits or thresholds are invalid");
    return c;
}
} // namespace

struct PitchEstimationParser::State {
    Configuration configuration{};
    std::vector<double> frequencies;
    std::vector<double> scores;
    size_t first = 0, last = 0;
};

PitchEstimationParser::PitchEstimationParser() = default;
PitchEstimationParser::~PitchEstimationParser() = default;

opk::Result<void> PitchEstimationParser::parse(const opk::TensorParser::Input &input,
                                               open_perception_kit::FrameResults &results) {
    const auto configuration = configure(input.attributes);
    if (!configuration)
        return tl::unexpected(configuration.error());
    const auto &c = *configuration;
    const auto *const tensor = input.tensors[c.tensorIndex];
    if (!tensor || !tensor->isValid() || tensor->getValueType() != opk::Dtype::Float32)
        return invalid("Pitch parser requires a Float32 score vector");
    const auto &shape = tensor->getShape();
    if (shape.rank < 1 || shape.rank > 8 ||
        shape.dims[shape.rank - 1] != static_cast<int64_t>(c.binCount))
        return invalid("Pitch parser score axis must match binCount");
    for (size_t axis = 0; axis + 1 < shape.rank; ++axis)
        if (shape.dims[axis] != 1)
            return invalid("Pitch parser requires singleton leading dimensions");
    const auto &audio = input.inferenceInfo.audio;
    if (audio.hopSampleCount == 0 || audio.windowEndSample <= audio.windowStartSample ||
        audio.hopSampleCount > audio.windowEndSample - audio.windowStartSample)
        return invalid("Pitch parser requires a complete scheduled audio window");
    if (audio.pcmRms && (!std::isfinite(*audio.pcmRms) || *audio.pcmRms < 0))
        return invalid("Pitch parser PCM RMS must be finite and nonnegative");
    if (c.minimumRms > 0 && !audio.pcmRms)
        return invalid("Pitch parser silence gating requires original PCM RMS");
    const auto frequency = [&c](const double bin) {
        return c.referenceHz * std::exp2((c.centsOffset + c.centsPerBin * bin) / 1200.0);
    };
    // Attributes can change between calls. Commit a new cache only after validating
    // the complete grid, and overwrite every score on each successful parse.
    if (!state_ || state_->configuration != c) {
        auto next = std::make_unique<State>();
        next->configuration = c;
        next->frequencies.resize(c.binCount);
        next->scores.resize(c.binCount);
        next->first = c.binCount;
        for (size_t i = 0; i < c.binCount; ++i) {
            const double hz = frequency(static_cast<double>(i));
            if (!std::isfinite(hz) || hz <= 0)
                return invalid("Pitch parser grid produces invalid frequencies");
            next->frequencies[i] = hz;
            if (hz < c.minHz || hz > c.maxHz)
                continue;
            next->first = std::min(next->first, i);
            next->last = i;
        }
        if (next->first == c.binCount)
            return invalid("Pitch parser frequency range contains no pitch bins");
        state_ = std::move(next);
    }
    auto &scores = state_->scores;
    const size_t first = state_->first, last = state_->last;
    size_t peak = first;
    double maximum = -1;
    for (size_t i = 0; i < c.binCount; ++i) {
        const double value = tensor->get(i);
        if (!std::isfinite(value) || (!c.sigmoid && (value < 0 || value > 1)))
            return invalid("Pitch parser requires finite logits or scores in [0,1]");
        if (c.sigmoid) {
            const double exponential = std::exp(-std::abs(value));
            scores[i] = value >= 0 ? 1 / (1 + exponential) : exponential / (1 + exponential);
        } else {
            scores[i] = value;
        }
        if (i < first || i > last)
            continue;
        if (scores[i] > maximum) { // Stable ties choose the lowest eligible bin.
            maximum = scores[i];
            peak = i;
        }
    }
    auto estimate = std::make_unique<open_perception_kit::metadata::PitchEstimateT>();
    estimate->object = open_perception_kit::makeObjectMeta(0U, input.inferenceInfo.parentId);
    estimate->periodicity = static_cast<float>(maximum);
    estimate->voiced = maximum > 0 &&
                       static_cast<float>(maximum) >= static_cast<float>(c.threshold) &&
                       (c.minimumRms == 0 || (audio.pcmRms && *audio.pcmRms >= c.minimumRms));
    if (*estimate->voiced) {
        double bin = static_cast<double>(peak);
        if (c.weighted) {
            const size_t begin = std::max(first, peak > c.radius ? peak - c.radius : size_t{0});
            const size_t end = std::min(last, peak + c.radius);
            double sum = 0, weightedSum = 0;
            for (size_t i = begin; i <= end; ++i) {
                sum += scores[i];
                weightedSum += static_cast<double>(i) * scores[i];
            }
            bin = weightedSum / sum;
        }
        estimate->frequency_hz =
            static_cast<float>(c.weighted ? frequency(bin) : state_->frequencies[peak]);
    }
    estimate->window_start_sample = audio.windowStartSample;
    estimate->window_end_sample = audio.windowEndSample;
    estimate->window_start_ts_ns = audio.windowStartTimestampNs;
    estimate->window_end_ts_ns = audio.windowEndTimestampNs;
    estimate->discontinuity = audio.discontinuity;
    if (!opk::audio_results::valid(*estimate))
        return invalid("Pitch parser result has invalid timing or frequency");
    open_perception_kit::metadata::PitchEstimatesT payload;
    payload.layer =
        open_perception_kit::makeLayerInfo({.model = input.inferenceInfo.modelName,
                                            .inferElementId = input.inferenceInfo.inferElementId,
                                            .producer = &input.producerInfo});
    if (input.attributes.getBoolOrDefault("log", false) && estimate->frequency_hz.has_value()) {
        fmt::print(stderr,
                   "Pitch [{}]: {:.2f} Hz (periodicity={:.3f})\n",
                   input.inferenceInfo.modelName,
                   estimate->frequency_hz.value_or(0.0f),
                   *estimate->periodicity);
    }
    payload.estimates.push_back(std::move(estimate));
    results.add(std::move(payload));
    return {};
}
} // namespace opk::stdop::postproc
