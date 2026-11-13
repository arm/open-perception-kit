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

#include "log_mel_reference.h"
#include "preproc/CpuLogMelSpectrogram.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>

namespace {
using opk::stdop::preproc::CpuLogMelSpectrogram;
using opk::stdop::preproc::LogMelSpectrogramConfig;

std::vector<float> waveform(const size_t count) {
    std::vector<float> samples(count);
    for (size_t i = 0; i < count; ++i) {
        samples[i] = static_cast<float>((static_cast<int>(i * 37 % 101) - 50) / 250.0 +
                                        (static_cast<int>(i * 13 % 31) - 15) / 150.0);
    }
    return samples;
}

std::vector<float> referenceWaveform(const size_t count) {
    auto samples = waveform(count);
    for (size_t i = 0; i < count; ++i) {
        if (i < 384)
            samples[i] = 0;
        if (i == 384)
            samples[i] = 0.9f;
    }
    return samples;
}

LogMelSpectrogramConfig windowConfig(const size_t window, const size_t hop) {
    auto config = log_mel_reference::cases[1].config;
    config.windowSampleCount = window;
    config.frameHopSamples = hop;
    return config;
}

LogMelSpectrogramConfig rollingConfig() {
    return log_mel_reference::cases[2].config;
}

std::span<uint8_t> bytes(std::vector<float> &values) {
    return {reinterpret_cast<uint8_t *>(values.data()), values.size() * sizeof(float)};
}

TEST(CpuLogMelSpectrogram, HandlesWrappedPcmAndUnalignedOutput) {
    CpuLogMelSpectrogram kernel;
    ASSERT_TRUE(kernel.configure(windowConfig(2000, 160), 32));
    const auto samples = waveform(2000);
    opk::PcmRingBuffer contiguous(2000);
    opk::PcmRingBuffer wrapped(2003);
    ASSERT_TRUE(contiguous.append(samples));
    ASSERT_TRUE(wrapped.append(std::array<float, 7>{}));
    ASSERT_TRUE(wrapped.append(samples));
    const auto view = wrapped.latest(2000);
    ASSERT_TRUE(view);
    ASSERT_FALSE(view->second().empty());
    std::vector<float> expected(size_t{11} * 32);
    ASSERT_TRUE(kernel.process(contiguous.view(), bytes(expected), 160, false));
    std::vector<uint8_t> storage(expected.size() * sizeof(float) + 1, 0xAB);
    ASSERT_TRUE(kernel.process(*view, std::span<uint8_t>(storage).subspan(1), 160, false));
    EXPECT_EQ(storage.front(), 0xAB);
    EXPECT_EQ(std::memcmp(storage.data() + 1, expected.data(), expected.size() * sizeof(float)), 0);
}

TEST(CpuLogMelSpectrogram, SilenceIsZeroAndInvalidFramesDoNotAdvanceStatisticsOrWrite) {
    CpuLogMelSpectrogram kernel;
    CpuLogMelSpectrogram reference;
    ASSERT_TRUE(kernel.configure(rollingConfig(), 24));
    ASSERT_TRUE(reference.configure(rollingConfig(), 24));
    opk::PcmRingBuffer silence(384);
    ASSERT_TRUE(silence.append(std::vector<float>(384)));
    std::vector<float> output(24, 17);
    std::vector<float> expected(24);
    ASSERT_TRUE(kernel.process(silence.view(), bytes(output), 128, false));
    ASSERT_TRUE(reference.process(silence.view(), bytes(expected), 128, false));
    EXPECT_EQ(output, std::vector<float>(24));
    for (const float bad : {std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::max()}) {
        opk::PcmRingBuffer invalid(384);
        ASSERT_TRUE(invalid.append(std::vector<float>(384, bad)));
        std::fill(output.begin(), output.end(), 17);
        EXPECT_FALSE(kernel.process(invalid.view(), bytes(output), 128, true));
        EXPECT_EQ(output, std::vector<float>(24, 17));
    }
    const auto samples = referenceWaveform(512);
    opk::PcmRingBuffer pcm(512);
    ASSERT_TRUE(pcm.append(samples));
    const auto second = pcm.window(128, 384);
    ASSERT_TRUE(second);
    ASSERT_TRUE(kernel.process(*second, bytes(output), 128, false));
    ASSERT_TRUE(reference.process(*second, bytes(expected), 128, false));
    EXPECT_EQ(output, expected);
    EXPECT_FALSE(kernel.process(*second, bytes(output).first(8), 128, false));
    EXPECT_FALSE(kernel.process(pcm.view(), bytes(output), 128, false));
}

TEST(CpuLogMelSpectrogram, RejectsUnsupportedConfiguration) {
    CpuLogMelSpectrogram kernel;
    const auto reject = [&](const auto modify) {
        auto config = windowConfig(2000, 160);
        modify(config);
        EXPECT_FALSE(kernel.configure(config, 24));
    };
    for (const size_t value : {0, 2, 3, 14, 4098})
        reject([value](auto &config) {
            config.fftSize = value;
            config.windowLengthSamples = std::min(size_t{320}, value);
        });
    for (const size_t value : {399, 960001})
        reject([value](auto &config) { config.windowSampleCount = value; });
    for (const size_t value : {0, 1, 401})
        reject([value](auto &config) { config.windowLengthSamples = value; });
    for (const size_t value : {0, 2001})
        reject([value](auto &config) { config.frameHopSamples = value; });
    for (const double value : {-1.0, 8000.0, std::numeric_limits<double>::quiet_NaN()})
        reject([value](auto &config) { config.minFrequencyHz = value; });
    for (const double value : {0.0, 8001.0, std::numeric_limits<double>::infinity()})
        reject([value](auto &config) { config.maxFrequencyHz = value; });
    for (const float value : {0.0f, -1.0f, std::numeric_limits<float>::infinity()})
        reject([value](auto &config) { config.logOffset = value; });
    reject([](auto &config) { config.layout = static_cast<LogMelSpectrogramConfig::Layout>(99); });
    for (const double value : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN()})
        reject([value](auto &config) {
            config.normalization = LogMelSpectrogramConfig::WindowNormalization{value};
        });
    for (const size_t count : {0, 4097}) {
        auto config = rollingConfig();
        config.normalization = LogMelSpectrogramConfig::RollingNormalization{1e-6, count};
        EXPECT_FALSE(kernel.configure(config, 24));
    }
    for (const double epsilon : {0.0, -1.0, std::numeric_limits<double>::infinity()}) {
        auto config = rollingConfig();
        config.normalization = LogMelSpectrogramConfig::RollingNormalization{epsilon, 7};
        EXPECT_FALSE(kernel.configure(config, 24));
    }
    auto config = rollingConfig();
    config.windowSampleCount = 512;
    EXPECT_FALSE(kernel.configure(config, 24));
    EXPECT_FALSE(kernel.configure(windowConfig(2000, 160), 0));
    EXPECT_FALSE(kernel.configure(windowConfig(2000, 160), 513));
    EXPECT_FALSE(kernel.configure(windowConfig(960000, 1), 32));
    EXPECT_FALSE(kernel.configure(windowConfig(400, 160), 1));
    config = windowConfig(2000, 160);
    config.minFrequencyHz = 7999.999999;
    config.maxFrequencyHz = 8000;
    EXPECT_FALSE(kernel.configure(config, 24));
}

TEST(CpuLogMelSpectrogram, RollingCadenceErrorsPreserveOutputAndHistory) {
    CpuLogMelSpectrogram kernel;
    CpuLogMelSpectrogram reference;
    const auto config = rollingConfig();
    ASSERT_TRUE(kernel.configure(config, 24));
    ASSERT_TRUE(reference.configure(config, 24));
    opk::PcmRingBuffer pcm(640);
    ASSERT_TRUE(pcm.append(referenceWaveform(640)));
    const auto first = pcm.window(0, 384);
    const auto second = pcm.window(128, 384);
    const auto third = pcm.window(256, 384);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(third);
    std::vector<float> output(24);
    std::vector<float> expected(24);
    ASSERT_TRUE(kernel.process(*first, bytes(output), 128, false));
    ASSERT_TRUE(reference.process(*first, bytes(expected), 128, false));
    std::fill(output.begin(), output.end(), 17.0f);
    EXPECT_FALSE(kernel.process(*first, bytes(output), 128, false));
    EXPECT_FALSE(kernel.process(*third, bytes(output), 128, false));
    EXPECT_FALSE(kernel.process(*second, bytes(output), 384, true));
    EXPECT_FALSE(kernel.process(*second, bytes(output).first(8), 128, true));
    EXPECT_EQ(output, std::vector<float>(24, 17.0f));
    ASSERT_TRUE(kernel.process(*second, bytes(output), 128, false));
    ASSERT_TRUE(reference.process(*second, bytes(expected), 128, false));
    EXPECT_EQ(output, expected);
    ASSERT_TRUE(kernel.process(*first, bytes(output), 128, true));
    EXPECT_EQ(output, std::vector<float>(24));
    ASSERT_TRUE(kernel.process(*second, bytes(output), 128, false));
    EXPECT_EQ(output, expected);
}

TEST(CpuLogMelSpectrogram, MatchesCompleteModelIndependentTorchTensorsInBothLayouts) {
    for (const auto &reference : log_mel_reference::cases) {
        SCOPED_TRACE(reference.name);
        for (const auto layout : {LogMelSpectrogramConfig::Layout::TimeMajor,
                                  LogMelSpectrogramConfig::Layout::MelMajor}) {
            auto config = reference.config;
            config.layout = layout;
            CpuLogMelSpectrogram kernel;
            ASSERT_TRUE(kernel.configure(config, reference.bins));
            const size_t sampleCount =
                config.windowSampleCount + (reference.windows - 1) * config.frameHopSamples;
            opk::PcmRingBuffer pcm(sampleCount);
            ASSERT_TRUE(pcm.append(waveform(sampleCount)));
            const size_t values = kernel.getOutputCount();
            ASSERT_EQ(reference.expected.size(), reference.windows * values);
            const size_t frames = values / reference.bins;
            std::vector<float> output(values);
            for (size_t pass = 0; pass < 2; ++pass) {
                for (size_t step = 0; step < reference.windows; ++step) {
                    const auto view =
                        pcm.window(step * config.frameHopSamples, config.windowSampleCount);
                    ASSERT_TRUE(view);
                    ASSERT_TRUE(
                        kernel.process(*view, bytes(output), config.frameHopSamples, step == 0));
                    for (size_t frame = 0; frame < frames; ++frame) {
                        for (size_t bin = 0; bin < reference.bins; ++bin) {
                            const size_t index =
                                layout == LogMelSpectrogramConfig::Layout::TimeMajor
                                    ? frame * reference.bins + bin
                                    : bin * frames + frame;
                            EXPECT_NEAR(
                                output[index],
                                reference.expected[step * values + frame * reference.bins + bin],
                                3e-4f)
                                << "step " << step << " frame " << frame << " bin " << bin;
                        }
                    }
                }
            }
        }
    }
}

TEST(CpuLogMelSpectrogram, LayoutDoesNotChangeNormalizationAndRawSilenceUsesConfiguredOffset) {
    const auto &reference = log_mel_reference::cases[1];
    opk::PcmRingBuffer pcm(reference.config.windowSampleCount);
    ASSERT_TRUE(pcm.append(waveform(reference.config.windowSampleCount)));
    CpuLogMelSpectrogram kernel;
    ASSERT_TRUE(kernel.configure(reference.config, reference.bins));
    std::vector<float> timeMajor(kernel.getOutputCount());
    ASSERT_TRUE(kernel.process(pcm.view(), bytes(timeMajor), 1, false));
    auto config = reference.config;
    config.layout = LogMelSpectrogramConfig::Layout::MelMajor;
    ASSERT_TRUE(kernel.configure(config, reference.bins));
    std::vector<float> melMajor(timeMajor.size());
    ASSERT_TRUE(kernel.process(pcm.view(), bytes(melMajor), 1, false));
    const size_t frames = timeMajor.size() / reference.bins;
    for (size_t frame = 0; frame < frames; ++frame) {
        for (size_t bin = 0; bin < reference.bins; ++bin)
            EXPECT_EQ(timeMajor[frame * reference.bins + bin], melMajor[bin * frames + frame]);
    }
    config = log_mel_reference::cases[0].config;
    ASSERT_TRUE(kernel.configure(config, 23));
    opk::PcmRingBuffer silence(config.windowSampleCount);
    ASSERT_TRUE(silence.append(std::vector<float>(config.windowSampleCount)));
    std::vector<float> raw(kernel.getOutputCount());
    ASSERT_TRUE(kernel.process(silence.view(), bytes(raw), 1, false));
    EXPECT_EQ(raw, std::vector<float>(raw.size(), std::log(config.logOffset)));
}

} // namespace
