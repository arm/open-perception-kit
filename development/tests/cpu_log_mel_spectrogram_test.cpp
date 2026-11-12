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
#include <numeric>

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
        if (i < 512 || (i >= 18000 && i < 24000))
            samples[i] = 0;
        if (i == 512)
            samples[i] = 0.9f;
    }
    return samples;
}

LogMelSpectrogramConfig windowConfig(const size_t window, const size_t hop) {
    return {window,
            512,
            480,
            hop,
            0,
            8000,
            1e-6f,
            LogMelSpectrogramConfig::Layout::TimeMajor,
            LogMelSpectrogramConfig::WindowNormalization{1e-8}};
}

LogMelSpectrogramConfig rollingConfig() {
    auto config = windowConfig(512, 400);
    config.normalization = LogMelSpectrogramConfig::RollingNormalization{1e-8, 100};
    return config;
}

std::span<uint8_t> bytes(std::vector<float> &values) {
    return {reinterpret_cast<uint8_t *>(values.data()), values.size() * sizeof(float)};
}

// Golden samples: torchaudio 2.9.1 MelSpectrogram, HTK/power2/FFT512/Hann480,
// center=False; natural log(+1e-6), float64 mean/sample std(+1e-8).
struct WindowCase {
    size_t window;
    size_t hop;
    size_t bins;
    std::array<float, 24> expected;
};

TEST(CpuLogMelSpectrogram, MatchesIndependentTorchWindowProfiles) {
    const std::array<WindowCase, 3> cases{{
        {8000, 400, 64, {-2.95529771f,  -2.95529771f,  0.21497938f,   0.897890568f, -0.172027618f,
                         -0.219647512f, 1.23216355f,   1.08454227f,   0.617847204f, -0.177407667f,
                         0.121604554f,  -0.185064957f, 1.49184573f,   0.54587692f,  0.234627023f,
                         0.451655954f,  0.484164983f,  -2.14899397f,  0.687092662f, 0.752281547f,
                         -0.134759024f, 0.132181376f,  -0.580163062f, 0.734555185f}},
        {16000, 160, 40, {-4.43065882f,   -0.159935609f, -0.290854603f, 0.64538002f,  0.465781003f,
                          -0.654383242f,  -0.592847407f, -0.400651097f, 0.177620813f, 1.11774528f,
                          -0.920870721f,  0.144536048f,  -0.19163242f,  0.301309526f, -1.25170732f,
                          -0.270542949f,  0.78378135f,   0.547755361f,  1.51282847f,  -1.27834821f,
                          -0.0604834557f, 0.136920437f,  0.644236147f,  0.838200748f}},
        {16000, 400, 64, {-3.43344164f,  0.336440504f,  0.42662105f,   -0.295525074f,
                          1.13840032f,   0.913579166f,  -0.736768663f, 0.471377909f,
                          0.400602669f,  0.0526370928f, 1.31590784f,   -0.0719527304f,
                          0.0580774397f, -2.48610067f,  0.512159288f,  -0.288591504f,
                          -0.946890891f, 0.571846366f,  -0.238686621f, -0.00606922153f,
                          1.60352635f,   0.514473557f,  -0.309915006f, 0.732630074f}},
    }};
    for (const auto &item : cases) {
        SCOPED_TRACE(item.bins);
        CpuLogMelSpectrogram kernel;
        ASSERT_TRUE(kernel.configure(windowConfig(item.window, item.hop), item.bins));
        opk::PcmRingBuffer pcm(item.window);
        ASSERT_TRUE(pcm.append(referenceWaveform(item.window)));
        const size_t count = (1 + (item.window - 512) / item.hop) * item.bins;
        std::vector<float> output(count);
        ASSERT_TRUE(kernel.process(pcm.view(), bytes(output), 400, false));
        for (size_t i = 0; i < item.expected.size(); ++i)
            EXPECT_NEAR(
                output[i * (count - 1) / (item.expected.size() - 1)], item.expected[i], 2e-4f);
        const double mean = std::accumulate(output.begin(), output.end(), 0.0) / count;
        double squared = 0;
        for (const float value : output)
            squared += (value - mean) * (value - mean);
        EXPECT_NEAR(mean, 0, 1e-7);
        EXPECT_NEAR(squared / (count - 1), 1, 1e-6);
    }
}

TEST(CpuLogMelSpectrogram, MatchesRollingOracleThroughEvictionAndReset) {
    CpuLogMelSpectrogram kernel;
    ASSERT_TRUE(kernel.configure(rollingConfig(), 64));
    const auto samples = referenceWaveform(42112);
    opk::PcmRingBuffer pcm(samples.size());
    ASSERT_TRUE(pcm.append(samples));
    const std::array<size_t, 10> bins{0, 1, 5, 13, 21, 31, 43, 53, 62, 63};
    const std::array<size_t, 5> frames{1, 2, 99, 100, 104};
    const std::array<std::array<float, 10>, 5> expected{{
        {0.613766849f,
         0.624329031f,
         0.596115172f,
         1.06241274f,
         0.907613099f,
         0.81920433f,
         1.22441196f,
         1.08482814f,
         1.29732776f,
         1.2230823f},
        {-1.02953398f,
         -0.947352767f,
         0.403227389f,
         0.800593317f,
         0.603807628f,
         0.551577866f,
         0.978468895f,
         0.770492136f,
         1.07031095f,
         1.00324535f},
        {-1.82734764f,
         -1.62418914f,
         0.103310443f,
         0.625956655f,
         0.356725574f,
         0.28386423f,
         0.827211916f,
         0.548972189f,
         0.912747025f,
         0.821230173f},
        {-1.80989611f,
         -1.64865315f,
         0.0809011757f,
         0.614281118f,
         0.339329779f,
         0.264780819f,
         0.815863371f,
         0.533618748f,
         0.906159282f,
         0.814402044f},
        {-1.69871974f,
         -1.59744656f,
         0.0830190629f,
         0.604529023f,
         0.336559504f,
         0.266746402f,
         0.821162879f,
         0.531581521f,
         0.905514717f,
         0.809012413f},
    }};
    std::vector<float> output(64);
    for (size_t frame = 0; frame <= 104; ++frame) {
        const auto view = pcm.window(frame * 400, 512);
        ASSERT_TRUE(view);
        ASSERT_TRUE(kernel.process(*view, bytes(output), 400, false));
        const auto found = std::find(frames.begin(), frames.end(), frame);
        if (found != frames.end()) {
            const size_t row = static_cast<size_t>(found - frames.begin());
            for (size_t i = 0; i < bins.size(); ++i)
                EXPECT_NEAR(output[bins[i]], expected[row][i], 3e-4f) << frame << ":" << bins[i];
        }
    }
    const auto first = pcm.window(0, 512);
    ASSERT_TRUE(first);
    ASSERT_TRUE(kernel.process(*first, bytes(output), 400, true));
    EXPECT_EQ(output, std::vector<float>(64));
    const auto second = pcm.window(400, 512);
    ASSERT_TRUE(second);
    ASSERT_TRUE(kernel.process(*second, bytes(output), 400, false));
    for (size_t i = 0; i < bins.size(); ++i)
        EXPECT_NEAR(output[bins[i]], expected[0][i], 3e-4f);
}

TEST(CpuLogMelSpectrogram, HandlesWrappedPcmAndUnalignedOutput) {
    CpuLogMelSpectrogram kernel;
    ASSERT_TRUE(kernel.configure(windowConfig(8000, 400), 64));
    const auto samples = referenceWaveform(8000);
    opk::PcmRingBuffer contiguous(8000);
    opk::PcmRingBuffer wrapped(8003);
    ASSERT_TRUE(contiguous.append(samples));
    ASSERT_TRUE(wrapped.append(std::array<float, 7>{}));
    ASSERT_TRUE(wrapped.append(samples));
    const auto view = wrapped.latest(8000);
    ASSERT_TRUE(view);
    ASSERT_FALSE(view->second().empty());
    std::vector<float> expected(size_t{19} * 64);
    ASSERT_TRUE(kernel.process(contiguous.view(), bytes(expected), 400, false));
    std::vector<uint8_t> storage(expected.size() * sizeof(float) + 1, 0xAB);
    ASSERT_TRUE(kernel.process(*view, std::span<uint8_t>(storage).subspan(1), 400, false));
    EXPECT_EQ(storage.front(), 0xAB);
    EXPECT_EQ(std::memcmp(storage.data() + 1, expected.data(), expected.size() * sizeof(float)), 0);
}

TEST(CpuLogMelSpectrogram, SilenceIsZeroAndInvalidFramesDoNotAdvanceStatisticsOrWrite) {
    CpuLogMelSpectrogram kernel;
    ASSERT_TRUE(kernel.configure(rollingConfig(), 64));
    opk::PcmRingBuffer silence(512);
    ASSERT_TRUE(silence.append(std::vector<float>(512)));
    std::vector<float> output(64, 17);
    ASSERT_TRUE(kernel.process(silence.view(), bytes(output), 400, false));
    EXPECT_EQ(output, std::vector<float>(64));
    for (const float bad : {std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::max()}) {
        opk::PcmRingBuffer invalid(512);
        ASSERT_TRUE(invalid.append(std::vector<float>(512, bad)));
        std::fill(output.begin(), output.end(), 17);
        EXPECT_FALSE(kernel.process(invalid.view(), bytes(output), 400, true));
        EXPECT_EQ(output, std::vector<float>(64, 17));
    }
    const auto samples = referenceWaveform(912);
    opk::PcmRingBuffer pcm(912);
    ASSERT_TRUE(pcm.append(samples));
    const auto second = pcm.window(400, 512);
    ASSERT_TRUE(second);
    ASSERT_TRUE(kernel.process(*second, bytes(output), 400, false));
    EXPECT_NEAR(output[0], 0.613766849f, 3e-4f);
    EXPECT_FALSE(kernel.process(*second, bytes(output).first(8), 400, false));
    EXPECT_FALSE(kernel.process(pcm.view(), bytes(output), 400, false));
}

TEST(CpuLogMelSpectrogram, RejectsUnsupportedConfiguration) {
    CpuLogMelSpectrogram kernel;
    const auto reject = [&](const auto modify) {
        auto config = windowConfig(8000, 400);
        modify(config);
        EXPECT_FALSE(kernel.configure(config, 64));
    };
    for (const size_t value : {0, 2, 3, 14, 4098})
        reject([value](auto &config) {
            config.fftSize = value;
            config.windowLengthSamples = std::min(size_t{480}, value);
        });
    for (const size_t value : {511, 960001})
        reject([value](auto &config) { config.windowSampleCount = value; });
    for (const size_t value : {0, 1, 513})
        reject([value](auto &config) { config.windowLengthSamples = value; });
    for (const size_t value : {0, 8001})
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
        config.normalization = LogMelSpectrogramConfig::RollingNormalization{1e-8, count};
        EXPECT_FALSE(kernel.configure(config, 64));
    }
    for (const double epsilon : {0.0, -1.0, std::numeric_limits<double>::infinity()}) {
        auto config = rollingConfig();
        config.normalization = LogMelSpectrogramConfig::RollingNormalization{epsilon, 7};
        EXPECT_FALSE(kernel.configure(config, 64));
    }
    auto config = rollingConfig();
    config.windowSampleCount = 912;
    EXPECT_FALSE(kernel.configure(config, 64));
    EXPECT_FALSE(kernel.configure(windowConfig(8000, 400), 0));
    EXPECT_FALSE(kernel.configure(windowConfig(8000, 400), 513));
    EXPECT_FALSE(kernel.configure(windowConfig(960000, 1), 64));
    EXPECT_FALSE(kernel.configure(windowConfig(512, 400), 1));
    config = windowConfig(8000, 400);
    config.minFrequencyHz = 7999.999999;
    EXPECT_FALSE(kernel.configure(config, 64));
}

TEST(CpuLogMelSpectrogram, RollingCadenceErrorsPreserveOutputAndHistory) {
    CpuLogMelSpectrogram kernel;
    CpuLogMelSpectrogram reference;
    const auto config = rollingConfig();
    ASSERT_TRUE(kernel.configure(config, 64));
    ASSERT_TRUE(reference.configure(config, 64));
    opk::PcmRingBuffer pcm(1312);
    ASSERT_TRUE(pcm.append(referenceWaveform(1312)));
    const auto first = pcm.window(0, 512);
    const auto second = pcm.window(400, 512);
    const auto third = pcm.window(800, 512);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(third);
    std::vector<float> output(64);
    std::vector<float> expected(64);
    ASSERT_TRUE(kernel.process(*first, bytes(output), 400, false));
    ASSERT_TRUE(reference.process(*first, bytes(expected), 400, false));
    std::fill(output.begin(), output.end(), 17.0f);
    EXPECT_FALSE(kernel.process(*first, bytes(output), 400, false));
    EXPECT_FALSE(kernel.process(*third, bytes(output), 400, false));
    EXPECT_FALSE(kernel.process(*second, bytes(output), 512, true));
    EXPECT_FALSE(kernel.process(*second, bytes(output).first(8), 400, true));
    EXPECT_EQ(output, std::vector<float>(64, 17.0f));
    ASSERT_TRUE(kernel.process(*second, bytes(output), 400, false));
    ASSERT_TRUE(reference.process(*second, bytes(expected), 400, false));
    EXPECT_EQ(output, expected);
    ASSERT_TRUE(kernel.process(*first, bytes(output), 400, true));
    EXPECT_EQ(output, std::vector<float>(64));
    ASSERT_TRUE(kernel.process(*second, bytes(output), 400, false));
    EXPECT_EQ(output, expected);
}

TEST(CpuLogMelSpectrogram, FailedReconfigurationDisablesExecutionAndSuccessfulRebindResetsHistory) {
    CpuLogMelSpectrogram kernel;
    const auto config = rollingConfig();
    auto collapsedFilters = config;
    collapsedFilters.minFrequencyHz = 7999.999999;
    const std::array<std::pair<LogMelSpectrogramConfig, size_t>, 2> invalid{{
        {config, 513},
        {collapsedFilters, 64},
    }};
    opk::PcmRingBuffer pcm(512);
    ASSERT_TRUE(pcm.append(referenceWaveform(512)));
    std::vector<float> output(64, 17.0f);
    for (const auto &[invalidConfig, bins] : invalid) {
        ASSERT_TRUE(kernel.configure(config, 64));
        EXPECT_EQ(kernel.getRequiredAudioSamples(), 512u);
        EXPECT_EQ(kernel.getOutputCount(), 64u);
        ASSERT_TRUE(kernel.process(pcm.view(), bytes(output), 400, false));
        EXPECT_FALSE(kernel.configure(invalidConfig, bins));
        EXPECT_EQ(kernel.getRequiredAudioSamples(), 0u);
        EXPECT_EQ(kernel.getOutputCount(), 0u);
        std::fill(output.begin(), output.end(), 17.0f);
        EXPECT_FALSE(kernel.process(pcm.view(), bytes(output), 400, false));
        EXPECT_EQ(output, std::vector<float>(64, 17.0f));
        ASSERT_TRUE(kernel.configure(config, 64));
        ASSERT_TRUE(kernel.process(pcm.view(), bytes(output), 400, false));
        EXPECT_EQ(output, std::vector<float>(64));
    }
}

TEST(CpuLogMelSpectrogram, ReconfigurationChangesNormalizationAndDiscardsPreviousHistory) {
    CpuLogMelSpectrogram kernel;
    const auto rolling = rollingConfig();
    const auto window = windowConfig(512, 400);
    auto raw = window;
    raw.normalization = std::monostate{};
    opk::PcmRingBuffer pcm(912);
    ASSERT_TRUE(pcm.append(referenceWaveform(912)));
    std::vector<float> output(64);
    std::vector<float> expected(64);
    for (const auto &config : {rolling, window, raw, rolling}) {
        CpuLogMelSpectrogram reference;
        ASSERT_TRUE(reference.configure(config, 64));
        ASSERT_TRUE(kernel.configure(config, 64));
        for (const size_t start : {0, 400}) {
            const auto view = pcm.window(start, 512);
            ASSERT_TRUE(view);
            ASSERT_TRUE(reference.process(*view, bytes(expected), 400, false));
            ASSERT_TRUE(kernel.process(*view, bytes(output), 400, false));
            EXPECT_EQ(output, expected);
        }
    }
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
