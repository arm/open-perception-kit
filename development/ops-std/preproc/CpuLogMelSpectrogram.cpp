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

#include "preproc/CpuLogMelSpectrogram.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <numeric>
#include <utility>

namespace opk::stdop::preproc {

namespace {

bool standardize(const std::span<float> features,
                 const double mean,
                 const double variance,
                 const double epsilon) {
    const double denominator = std::sqrt(variance) + epsilon;
    for (float &value : features) {
        value = static_cast<float>((value - mean) / denominator);
        if (!std::isfinite(value))
            return false;
    }
    return true;
}

} // namespace

bool CpuLogMelSpectrogram::WindowNormalization::normalize(const std::span<float> features,
                                                          const FrameTiming &) const {
    const double sum = std::accumulate(features.begin(), features.end(), 0.0);
    const double mean = sum / static_cast<double>(features.size());
    double variance = 0;
    for (const float value : features) {
        const double centered = value - mean;
        variance += centered * centered;
    }
    variance /= static_cast<double>(features.size() - 1);
    return standardize(features, mean, variance, epsilon);
}

CpuLogMelSpectrogram::RollingNormalization::RollingNormalization(const double epsilon,
                                                                 const size_t historyFrames)
    : epsilon(epsilon), moments(historyFrames) {}

bool CpuLogMelSpectrogram::RollingNormalization::normalize(const std::span<float> features,
                                                           const FrameTiming &timing) {
    if (timing.hopSampleCount != timing.frameHopSamples ||
        (!timing.discontinuity && nextPosition && *nextPosition != timing.beginPosition))
        return false;
    const double sum = std::accumulate(features.begin(), features.end(), 0.0);
    double mean = sum / static_cast<double>(features.size());
    double secondMoment = 0;
    for (const float value : features)
        secondMoment += static_cast<double>(value) * value;
    const std::pair<double, double> currentMoment{
        mean, secondMoment / static_cast<double>(features.size())};
    const size_t nextSlot = timing.discontinuity ? 0 : position;
    const size_t nextCount = timing.discontinuity ? 1 : std::min(count + 1, moments.size());
    mean = 0;
    secondMoment = 0;
    for (size_t i = 0; i < nextCount; ++i) {
        const auto &moment = i == nextSlot ? currentMoment : moments[i];
        mean += moment.first;
        secondMoment += moment.second;
    }
    mean /= static_cast<double>(nextCount);
    const double variance =
        std::max(0.0, secondMoment / static_cast<double>(nextCount) - mean * mean);
    if (!standardize(features, mean, variance, epsilon))
        return false;
    // No fallible operation remains before the caller publishes the validated features.
    moments[nextSlot] = currentMoment;
    count = nextCount;
    position = (nextSlot + 1) % moments.size();
    nextPosition = timing.beginPosition + timing.hopSampleCount;
    return true;
}

CpuLogMelSpectrogram::CpuLogMelSpectrogram() = default;
CpuLogMelSpectrogram::~CpuLogMelSpectrogram() = default;

bool LogMelSpectrogramConfig::isValid() const {
    if (fftSize < 4 || fftSize > 4096 || fftSize % 2 != 0 || windowSampleCount < fftSize ||
        windowSampleCount > 960000 || windowLengthSamples < 2 || windowLengthSamples > fftSize ||
        frameHopSamples == 0 || frameHopSamples > windowSampleCount ||
        !std::isfinite(minFrequencyHz) || !std::isfinite(maxFrequencyHz) || minFrequencyHz < 0 ||
        minFrequencyHz >= maxFrequencyHz || maxFrequencyHz > 8000 || !std::isfinite(logOffset) ||
        logOffset <= 0 || (layout != Layout::TimeMajor && layout != Layout::MelMajor))
        return false;
    size_t remaining = fftSize;
    for (const size_t factor : {2, 3, 5}) {
        while (remaining % factor == 0)
            remaining /= factor;
    }
    if (remaining != 1)
        return false;
    if (const auto *const window = std::get_if<WindowNormalization>(&normalization))
        return std::isfinite(window->epsilon) && window->epsilon > 0;
    if (const auto *const rolling = std::get_if<RollingNormalization>(&normalization))
        return std::isfinite(rolling->epsilon) && rolling->epsilon > 0 &&
               rolling->historyFrames >= 1 && rolling->historyFrames <= 4096 &&
               windowSampleCount == fftSize;
    return true;
}

bool CpuLogMelSpectrogram::configure(const LogMelSpectrogramConfig &config, const size_t melBins) {
    fft_.reset();
    normalization_.emplace<NoNormalization>();
    features_.clear();
    if (!config.isValid() || melBins == 0 || melBins > 512)
        return false;
    const size_t frames = 1 + (config.windowSampleCount - config.fftSize) / config.frameHopSamples;
    if (frames > 1048576 / melBins ||
        (std::holds_alternative<LogMelSpectrogramConfig::WindowNormalization>(
             config.normalization) &&
         frames * melBins < 2))
        return false;
    const size_t frequencyBins = config.fftSize / 2 + 1;
    buildHannWindow(config);
    if (!buildMelFilterbank(config, melBins))
        return false;
    fftInput_.resize(config.fftSize);
    spectrum_.resize(frequencyBins);
    power_.resize(frequencyBins);
    fft_.reset(gst_fft_f32_new(static_cast<int>(config.fftSize), FALSE));
    if (!fft_)
        return false;
    windowSampleCount_ = config.windowSampleCount;
    frameHopSamples_ = config.frameHopSamples;
    melBins_ = melBins;
    logOffset_ = config.logOffset;
    layout_ = config.layout;
    features_.resize(frames * melBins);
    struct NormalizationFactory {
        Normalization operator()(std::monostate) const {
            return NoNormalization{};
        }
        Normalization
        operator()(const LogMelSpectrogramConfig::WindowNormalization &settings) const {
            return WindowNormalization{settings.epsilon};
        }
        Normalization
        operator()(const LogMelSpectrogramConfig::RollingNormalization &settings) const {
            return RollingNormalization{settings.epsilon, settings.historyFrames};
        }
    };
    normalization_ = std::visit(NormalizationFactory{}, config.normalization);
    return true;
}

void CpuLogMelSpectrogram::buildHannWindow(const LogMelSpectrogramConfig &config) {
    window_.assign(config.fftSize, 0.0f);
    const size_t windowStart = (config.fftSize - config.windowLengthSamples) / 2;
    const float phaseStep = static_cast<float>(2.0 * std::numbers::pi /
                                               static_cast<double>(config.windowLengthSamples));
    for (size_t i = 0; i < config.windowLengthSamples; ++i)
        window_[i + windowStart] = 0.5f - 0.5f * std::cos(phaseStep * static_cast<float>(i));
}

bool CpuLogMelSpectrogram::buildMelFilterbank(const LogMelSpectrogramConfig &config,
                                              const size_t melBins) {
    const size_t frequencyBins = config.fftSize / 2 + 1;
    filters_.resize(melBins * frequencyBins);
    const auto htk = [](const double hz) {
        return static_cast<float>(2595 * std::log10(1 + hz / 700));
    };
    const float minimumMel = htk(config.minFrequencyHz);
    const float maximumMel = htk(config.maxFrequencyHz);
    const auto melFrequency = [minimumMel, maximumMel, melBins](const size_t point) {
        const float mel = minimumMel + (maximumMel - minimumMel) * static_cast<float>(point) /
                                           static_cast<float>(melBins + 1);
        return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f);
    };
    for (size_t bin = 0; bin < melBins; ++bin) {
        const float left = melFrequency(bin);
        const float center = melFrequency(bin + 1);
        const float right = melFrequency(bin + 2);
        if (!(left < center && center < right))
            return false;
        for (size_t frequency = 0; frequency < frequencyBins; ++frequency) {
            const float hz =
                static_cast<float>(frequency) * 16000.0f / static_cast<float>(config.fftSize);
            const float weight = std::max(
                0.0f, std::min((hz - left) / (center - left), (right - hz) / (right - center)));
            if (!std::isfinite(weight))
                return false;
            filters_[bin * frequencyBins + frequency] = weight;
        }
    }
    return true;
}

bool CpuLogMelSpectrogram::process(const PcmRingBuffer::View &pcm,
                                   const std::span<uint8_t> destination,
                                   const size_t hopSampleCount,
                                   const bool discontinuity) {
    if (!fft_ || pcm.size() != windowSampleCount_ || !destination.data() ||
        destination.size() != features_.size() * sizeof(float))
        return false;
    if (!extractFeatures(pcm))
        return false;
    const FrameTiming timing{pcm.beginPosition(), hopSampleCount, frameHopSamples_, discontinuity};
    if (!std::visit([&](auto &normalization) { return normalization.normalize(features_, timing); },
                    normalization_))
        return false;
    publishOutput(destination);
    return true;
}

bool CpuLogMelSpectrogram::extractFeatures(const PcmRingBuffer::View &pcm) {
    const auto first = pcm.first();
    const auto second = pcm.second();
    const auto finite = [](const float sample) { return std::isfinite(sample); };
    if (!std::all_of(first.begin(), first.end(), finite) ||
        !std::all_of(second.begin(), second.end(), finite))
        return false;
    const size_t frames = features_.size() / melBins_;
    for (size_t frame = 0; frame < frames; ++frame) {
        if (!computePowerFrame(pcm, frame * frameHopSamples_) ||
            !projectLogMelFrame(std::span<float>(features_).subspan(frame * melBins_, melBins_)))
            return false;
    }
    return true;
}

bool CpuLogMelSpectrogram::computePowerFrame(const PcmRingBuffer::View &pcm, const size_t start) {
    const auto first = pcm.first();
    const auto second = pcm.second();
    for (size_t i = 0; i < window_.size(); ++i) {
        const size_t position = start + i;
        const float sample =
            position < first.size() ? first[position] : second[position - first.size()];
        fftInput_[i] = sample * window_[i];
    }
    gst_fft_f32_fft(fft_.get(), fftInput_.data(), spectrum_.data());
    for (size_t i = 0; i < power_.size(); ++i) {
        power_[i] = spectrum_[i].r * spectrum_[i].r + spectrum_[i].i * spectrum_[i].i;
        if (!std::isfinite(power_[i]))
            return false;
    }
    return true;
}

bool CpuLogMelSpectrogram::projectLogMelFrame(const std::span<float> features) const {
    for (size_t bin = 0; bin < melBins_; ++bin) {
        float mel = 0;
        for (size_t i = 0; i < power_.size(); ++i)
            mel += power_[i] * filters_[bin * power_.size() + i];
        const float feature = std::log(mel + logOffset_);
        if (!std::isfinite(feature))
            return false;
        features[bin] = feature;
    }
    return true;
}

void CpuLogMelSpectrogram::publishOutput(const std::span<uint8_t> destination) const noexcept {
    if (layout_ == LogMelSpectrogramConfig::Layout::TimeMajor) {
        std::memcpy(destination.data(), features_.data(), destination.size());
    } else {
        const size_t frames = features_.size() / melBins_;
        for (size_t bin = 0; bin < melBins_; ++bin) {
            for (size_t frame = 0; frame < frames; ++frame)
                std::memcpy(destination.data() + (bin * frames + frame) * sizeof(float),
                            &features_[frame * melBins_ + bin],
                            sizeof(float));
        }
    }
}

} // namespace opk::stdop::preproc
