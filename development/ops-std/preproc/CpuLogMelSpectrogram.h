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

#include "opk/PcmRingBuffer.h"
#include <gst/fft/gstfftf32.h>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

namespace opk::stdop::preproc {

struct LogMelSpectrogramConfig {
    enum class Layout { TimeMajor, MelMajor };
    struct WindowNormalization {
        double epsilon;
    };
    struct RollingNormalization {
        double epsilon;
        size_t historyFrames;
    };
    size_t windowSampleCount;
    size_t fftSize;
    size_t windowLengthSamples;
    size_t frameHopSamples;
    double minFrequencyHz;
    double maxFrequencyHz;
    float logOffset;
    Layout layout;
    std::variant<std::monostate, WindowNormalization, RollingNormalization> normalization;

    bool isValid() const;
};

/// Periodic Hann, power-squared HTK log-mel at 16 kHz. Owns no borrowed PCM.
class CpuLogMelSpectrogram {
  public:
    CpuLogMelSpectrogram();
    ~CpuLogMelSpectrogram();
    bool configure(const LogMelSpectrogramConfig &config, const size_t melBins);
    size_t getRequiredAudioSamples() const {
        return fft_ ? windowSampleCount_ : 0;
    }
    size_t getOutputCount() const {
        return features_.size();
    }
    /// Failed processing leaves destination and rolling statistics unchanged.
    bool process(const PcmRingBuffer::View &pcm,
                 const std::span<uint8_t> destination,
                 const size_t hopSampleCount,
                 const bool discontinuity);

  private:
    struct FrameTiming {
        uint64_t beginPosition;
        size_t hopSampleCount;
        size_t frameHopSamples;
        bool discontinuity;
    };
    struct NoNormalization {
        bool normalize(const std::span<float>, const FrameTiming &) const {
            return true;
        }
    };
    struct WindowNormalization {
        double epsilon;
        bool normalize(const std::span<float> features, const FrameTiming &) const;
    };
    struct RollingNormalization {
        RollingNormalization(const double epsilon, const size_t historyFrames);
        bool normalize(const std::span<float> features, const FrameTiming &timing);

        double epsilon;
        std::vector<std::pair<double, double>> moments;
        size_t count = 0;
        size_t position = 0;
        std::optional<uint64_t> nextPosition;
    };
    using Normalization = std::variant<NoNormalization, WindowNormalization, RollingNormalization>;

    void buildHannWindow(const LogMelSpectrogramConfig &config);
    bool buildMelFilterbank(const LogMelSpectrogramConfig &config, const size_t melBins);
    bool extractFeatures(const PcmRingBuffer::View &pcm);
    bool computePowerFrame(const PcmRingBuffer::View &pcm, const size_t start);
    bool projectLogMelFrame(const std::span<float> features) const;
    void publishOutput(const std::span<uint8_t> destination) const noexcept;

    Normalization normalization_;
    std::unique_ptr<GstFFTF32, decltype(&gst_fft_f32_free)> fft_{nullptr, gst_fft_f32_free};
    std::vector<float> window_;
    std::vector<float> fftInput_;
    std::vector<GstFFTF32Complex> spectrum_;
    std::vector<float> power_;
    std::vector<float> filters_;
    std::vector<float> features_;
    size_t windowSampleCount_ = 0;
    size_t frameHopSamples_ = 0;
    size_t melBins_ = 0;
    float logOffset_ = 0;
    LogMelSpectrogramConfig::Layout layout_ = LogMelSpectrogramConfig::Layout::TimeMajor;
};

} // namespace opk::stdop::preproc
