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

#include "opk/PcmRingBuffer.h"
#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <gtest/gtest.h>
#include <limits>
#include <numbers>
#include <random>

namespace {
std::vector<float> values(const opk::PcmRingBuffer::View &view) {
    std::vector<float> result(view.size());
    EXPECT_TRUE(view.copyTo(result));
    return result;
}

TEST(PcmRingBuffer, CopiesLatestWithLeftPaddingWithoutAdvancingHistory) {
    opk::PcmRingBuffer ring(5);
    std::vector<float> output(5, -1);
    ASSERT_TRUE(ring.copyLatestPadded(output));
    EXPECT_EQ(output, (std::vector<float>{0, 0, 0, 0, 0}));
    ASSERT_TRUE(ring.append(std::vector<float>{1, 2, 3}));
    ASSERT_TRUE(ring.copyLatestPadded(output));
    EXPECT_EQ(output, (std::vector<float>{0, 0, 1, 2, 3}));
    EXPECT_EQ(ring.size(), 3u);
    EXPECT_EQ(ring.beginPosition(), 0u);
    EXPECT_EQ(ring.endPosition(), 3u);
    EXPECT_FALSE(ring.latest(5)); // Padding is not real history.

    ASSERT_TRUE(ring.append(std::vector<float>{4, 5, 6, 7}));
    ASSERT_TRUE(ring.copyLatestPadded(output));
    EXPECT_EQ(output, (std::vector<float>{3, 4, 5, 6, 7}));
    output.resize(3);
    ASSERT_TRUE(ring.copyLatestPadded(output));
    EXPECT_EQ(output, (std::vector<float>{5, 6, 7}));
    EXPECT_EQ(ring.endPosition(), 7u);

    ring.clear(100);
    ASSERT_TRUE(ring.copyLatestPadded(output));
    EXPECT_EQ(output, (std::vector<float>{0, 0, 0}));
    EXPECT_EQ(ring.endPosition(), 100u);
    EXPECT_TRUE(ring.empty());
}

TEST(PcmRingBuffer, PaddedCopyRejectsOversizeAndAliasingWithoutWriting) {
    opk::PcmRingBuffer ring(3);
    ASSERT_TRUE(ring.append(std::vector<float>{1, 2, 3}));
    std::vector<float> output(4, -1);
    EXPECT_FALSE(ring.copyLatestPadded(output));
    EXPECT_EQ(output, (std::vector<float>{-1, -1, -1, -1}));
    const auto borrowed = ring.view().first();
    EXPECT_FALSE(ring.copyLatestPadded(
        std::span<float>(const_cast<float *>(borrowed.data()), borrowed.size())));
    EXPECT_EQ(values(ring.view()), (std::vector<float>{1, 2, 3}));
    EXPECT_TRUE(ring.copyLatestPadded({}));

    opk::PcmRingBuffer zero(0);
    EXPECT_TRUE(zero.copyLatestPadded({}));
    EXPECT_FALSE(zero.copyLatestPadded(output));
    EXPECT_EQ(output, (std::vector<float>{-1, -1, -1, -1}));
}

TEST(PcmRingBuffer, ExposesOnlyReceivedSamplesAndCheckedWindows) {
    opk::PcmRingBuffer ring(5);
    EXPECT_TRUE(ring.view().empty());
    EXPECT_TRUE(ring.latest(0));
    EXPECT_FALSE(ring.latest(1));
    const std::array<float, 3> samples = {1, 2, 3};
    ASSERT_TRUE(ring.append(samples));
    EXPECT_EQ(values(ring.view()), (std::vector<float>{1, 2, 3}));
    EXPECT_EQ(ring.beginPosition(), 0u);
    EXPECT_EQ(ring.endPosition(), 3u);
    const auto window1 = ring.window(1, 2);
    if (!window1.has_value())
        FAIL() << "Expected window1 to contain a value";
    EXPECT_EQ(values(*window1), (std::vector<float>{2, 3}));
    EXPECT_FALSE(ring.window(2, 2));
    EXPECT_FALSE(ring.window(4, 0));
    EXPECT_TRUE(ring.window(3, 0));
    std::vector<float> small(2, -1);
    EXPECT_FALSE(ring.view().copyTo(small));
    EXPECT_EQ(small, (std::vector<float>{-1, -1}));
    std::vector<float> large(4, -1);
    EXPECT_TRUE(ring.view().copyTo(large));
    EXPECT_EQ(large, (std::vector<float>{1, 2, 3, -1}));
}

TEST(PcmRingBuffer, WrapsAndRetainsTailOfOversizedWrites) {
    opk::PcmRingBuffer ring(4);
    const std::array<float, 3> initial = {0, 1, 2};
    const std::array<float, 3> next = {3, 4, 5};
    ASSERT_TRUE(ring.append(initial));
    ASSERT_TRUE(ring.append(next));
    EXPECT_EQ(values(ring.view()), (std::vector<float>{2, 3, 4, 5}));
    EXPECT_FALSE(ring.view().second().empty());
    EXPECT_FALSE(ring.window(1, 1));
    const auto window2 = ring.window(3, 2);
    if (!window2.has_value())
        FAIL() << "Expected window2 to contain a value";
    EXPECT_EQ(values(*window2), (std::vector<float>{3, 4}));
    const std::array<float, 9> oversized = {6, 7, 8, 9, 10, 11, 12, 13, 14};
    ASSERT_TRUE(ring.append(oversized));
    EXPECT_EQ(values(ring.view()), (std::vector<float>{11, 12, 13, 14}));
    EXPECT_EQ(ring.beginPosition(), 11u);
    EXPECT_EQ(ring.endPosition(), 15u);
    const auto window3 = ring.latest(2);
    if (!window3.has_value())
        FAIL() << "Expected window3 to contain a value";
    EXPECT_EQ(values(*window3), (std::vector<float>{13, 14}));
}

TEST(PcmRingBuffer, ClearZeroCapacityOverflowAndAliasingHaveExplicitBehavior) {
    opk::PcmRingBuffer zero(0);
    const std::array<float, 2> samples = {1, 2};
    ASSERT_TRUE(zero.append(samples));
    EXPECT_TRUE(zero.empty());
    EXPECT_EQ(zero.beginPosition(), 2u);
    EXPECT_EQ(zero.endPosition(), 2u);
    EXPECT_FALSE(zero.window(1, 0));
    opk::PcmRingBuffer ring(4);
    ASSERT_TRUE(ring.append(samples));
    EXPECT_FALSE(ring.append(ring.view().first()));
    EXPECT_EQ(values(ring.view()), (std::vector<float>{1, 2}));
    ring.clear(std::numeric_limits<uint64_t>::max() - 1);
    EXPECT_FALSE(ring.append(samples));
    EXPECT_TRUE(ring.empty());
    ASSERT_TRUE(ring.append(std::span(samples).first(1)));
    EXPECT_EQ(ring.endPosition(), std::numeric_limits<uint64_t>::max());
    EXPECT_TRUE(ring.append({}));
    EXPECT_FALSE(ring.append(samples));
    EXPECT_EQ(values(ring.view()), (std::vector<float>{1}));
    ring.clear();
    EXPECT_TRUE(ring.empty());
    EXPECT_EQ(ring.endPosition(), 0u);
    EXPECT_EQ(ring.capacity(), 4u);
}

TEST(PcmRingBuffer, MatchesReferenceHistoryAcrossRandomChunkBoundaries) {
    // Fixed seed keeps randomized regression coverage reproducible.
    // NOLINTNEXTLINE(bugprone-random-generator-seed)
    std::mt19937 random(12345);
    for (size_t capacity : {0u, 1u, 7u, 64u}) {
        opk::PcmRingBuffer ring(capacity);
        std::deque<float> reference;
        uint64_t end = 0;
        for (size_t iteration = 0; iteration < 300; ++iteration) {
            std::vector<float> input(random() % 100);
            for (auto &value : input) {
                value = static_cast<float>(end++);
                reference.push_back(value);
                if (reference.size() > capacity)
                    reference.pop_front();
            }
            ASSERT_TRUE(ring.append(input));
            EXPECT_EQ(values(ring.view()), std::vector<float>(reference.begin(), reference.end()));
            EXPECT_EQ(ring.beginPosition(), end - reference.size());
            EXPECT_EQ(ring.endPosition(), end);
            for (size_t offset = 0; offset <= reference.size(); ++offset) {
                const size_t count = (reference.size() - offset) / 2;
                auto window = ring.window(ring.beginPosition() + offset, count);
                if (!window.has_value())
                    FAIL() << "Expected window to contain a value";
                EXPECT_EQ(values(*window),
                          std::vector<float>(reference.begin() + offset,
                                             reference.begin() + offset + count));
            }
        }
    }
}

template <typename T> std::span<const uint8_t> bytesOf(std::span<const T> samples) {
    return {reinterpret_cast<const uint8_t *>(samples.data()), samples.size_bytes()};
}

TEST(PcmRingBuffer, DecodesFormatsAndDownmixesWithoutResampling) {
    using Type = opk::AudioSampleType;
    opk::PcmRingBuffer ring(8);
    const std::array<uint8_t, 6> u8 = {0, 128, 128, 128, 255, 255};
    ASSERT_TRUE(ring.feed(u8, Type::U8, 2, 16000));
    EXPECT_EQ(values(ring.view()), (std::vector<float>{-0.5f, 0, 127.0f / 128}));
    ring.clear();
    const std::array<int16_t, 4> s16 = {-32768, 0, 16384, 16384};
    // Deliberately unaligned source must work without typed pointer dereferences.
    std::vector<uint8_t> unaligned(sizeof(s16) + 1);
    std::memcpy(unaligned.data() + 1, s16.data(), sizeof(s16));
    ASSERT_TRUE(ring.feed(std::span(unaligned).subspan(1), Type::S16, 2, 16000));
    EXPECT_EQ(values(ring.view()), (std::vector<float>{-0.5f, 0.5f}));
    ring.clear();
    const std::array<uint8_t, 9> s24 = {0, 0, 128, 0, 0, 64, 255, 255, 255};
    ASSERT_TRUE(ring.feed(s24, Type::S24, 1, 16000));
    EXPECT_EQ(values(ring.view()), (std::vector<float>{-1, 0.5f, -1.0f / 8388608}));
    ring.clear();
    const std::array<int32_t, 2> s32 = {std::numeric_limits<int32_t>::min(), 1073741824};
    ASSERT_TRUE(ring.feed(bytesOf<int32_t>(s32), Type::S32, 1, 16000));
    EXPECT_EQ(values(ring.view()), (std::vector<float>{-1, 0.5f}));
    ring.clear();
    const std::array<float, 4> f32 = {-0.75f, 0.25f, 0.5f, 0.5f};
    ASSERT_TRUE(ring.feed(bytesOf<float>(f32), Type::F32, 2, 16000));
    EXPECT_EQ(values(ring.view()), (std::vector<float>{-0.25f, 0.5f}));
}

TEST(PcmRingBuffer, RejectsInvalidFeedsWithoutChangingStream) {
    using Type = opk::AudioSampleType;
    opk::PcmRingBuffer ring(4);
    const std::array<uint8_t, 2> pcm = {128, 192};
    EXPECT_FALSE(ring.feed(pcm, Type::U8, 0, 16000));
    EXPECT_FALSE(ring.feed(pcm, Type::U8, 1, 0));
    EXPECT_FALSE(ring.feed(pcm, Type::U8, 1, 384001));
    EXPECT_FALSE(ring.feed(pcm, static_cast<Type>(99), 1, 16000));
    EXPECT_FALSE(ring.feed(pcm, Type::S24, 1, 16000));
    EXPECT_FALSE(ring.feed(pcm, Type::S32, std::numeric_limits<size_t>::max(), 16000));
    const std::array<float, 1> invalid = {std::numeric_limits<float>::quiet_NaN()};
    EXPECT_FALSE(ring.feed(bytesOf<float>(invalid), Type::F32, 1, 16000));
    ASSERT_TRUE(ring.feed(pcm, Type::U8, 1, 16000));
    EXPECT_FALSE(ring.feed(pcm, Type::U8, 1, 48000));
    EXPECT_FALSE(ring.feed(pcm, Type::S16, 1, 16000));
    EXPECT_FALSE(ring.feed(pcm, Type::U8, 2, 16000));
    EXPECT_EQ(values(ring.view()), (std::vector<float>{0, 0.5f}));
    ring.clear(std::numeric_limits<uint64_t>::max() - 1);
    EXPECT_FALSE(ring.feed(pcm, Type::U8, 1, 16000));
    EXPECT_TRUE(ring.empty());
    EXPECT_TRUE(ring.feed(std::span(pcm).first(1), Type::U8, 1, 16000));
    EXPECT_EQ(ring.endPosition(), std::numeric_limits<uint64_t>::max());
}

TEST(PcmRingBuffer, ResamplingIsIndependentOfChunkSizesAndResetDiscardsHistory) {
    for (size_t rate : {8000u, 11025u, 16000u, 22050u, 44100u, 47999u, 48000u, 96000u}) {
        SCOPED_TRACE(rate);
        std::vector<float> input(rate / 5 + 17);
        for (size_t i = 0; i < input.size(); ++i)
            input[i] = 0.6f * std::sin(2 * std::numbers::pi * 1000 * i / rate);
        opk::PcmRingBuffer whole(10000), chunks(10000);
        ASSERT_TRUE(whole.feed(bytesOf<float>(input), opk::AudioSampleType::F32, 1, rate));
        // Fixed seed keeps chunk boundaries reproducible.
        // NOLINTNEXTLINE(bugprone-random-generator-seed)
        std::mt19937 random(77);
        for (size_t offset = 0; offset < input.size();) {
            const size_t count =
                std::min(input.size() - offset, static_cast<size_t>(1 + random() % 31));
            ASSERT_TRUE(
                chunks.feed(bytesOf<float>(std::span<const float>(input).subspan(offset, count)),
                            opk::AudioSampleType::F32,
                            1,
                            rate));
            offset += count;
            EXPECT_EQ(chunks.endPosition(), offset * 16000 / rate);
        }
        EXPECT_EQ(whole.endPosition(), input.size() * 16000 / rate);
        EXPECT_EQ(values(whole.view()), values(chunks.view()));
        chunks.clear();
        ASSERT_TRUE(chunks.feed(bytesOf<float>(input), opk::AudioSampleType::F32, 1, rate));
        EXPECT_EQ(values(whole.view()), values(chunks.view()));
    }
}

TEST(PcmRingBuffer, ResamplingMatchesDirectConvolutionAcrossPhaseCyclesAndRateChanges) {
    opk::PcmRingBuffer ring(4096);
    // Include fractional up/downsampling, integer ratios, and rates whose full
    // phase cycle exceeds the cache budget. Reuse the buffer to test cache reset.
    for (size_t rate : {8000u, 11025u, 22050u, 44100u, 48000u, 47999u, 383999u, 384000u}) {
        SCOPED_TRACE(rate);
        ring.clear();
        std::vector<float> input(rate / 10 + 1);
        for (size_t i = 0; i < input.size(); ++i)
            input[i] = static_cast<float>(0.4 * std::sin(2 * std::numbers::pi * 731 * i / rate) +
                                          0.2 * std::cos(2 * std::numbers::pi * 2311 * i / rate));
        ASSERT_TRUE(ring.feed(bytesOf<float>(input), opk::AudioSampleType::F32, 1, rate));
        const auto output = values(ring.view());
        ASSERT_EQ(output.size(), input.size() * 16000 / rate);

        // Compute each expected sample directly from its absolute timestamp and
        // input samples, without streaming state or reuse of filter coefficients.
        const double cutoff = 0.9 * std::min(1.0, 16000.0 / rate);
        const auto radius = static_cast<size_t>(std::ceil(16.0 / cutoff));
        std::vector<double> weights(2 * radius + 1);
        for (size_t j = 0; j < output.size(); ++j) {
            const size_t target = (j + 1) * rate;
            const size_t consumed = (target + 15999) / 16000;
            const double fraction = static_cast<double>(consumed * 16000 - target) / 16000;
            double total = 0;
            for (size_t k = 0; k < weights.size(); ++k) {
                const double distance = static_cast<double>(radius) - k + fraction;
                const double x = std::numbers::pi * cutoff * distance;
                const double window =
                    std::abs(distance) < radius
                        ? 0.5 * (1 + std::cos(std::numbers::pi * distance / radius))
                        : 0;
                weights[k] = cutoff * (std::abs(x) < 1e-12 ? 1 : std::sin(x) / x) * window;
                total += weights[k];
            }
            double expected = 0;
            for (size_t k = 0; k < weights.size(); ++k)
                expected += (weights[k] / total) * (k < consumed ? input[consumed - 1 - k] : 0.0f);
            EXPECT_EQ(output[j], static_cast<float>(expected)) << "output sample " << j;
        }
    }
}

TEST(PcmRingBuffer, ResamplingPreservesSpeechBandAndRejectsAliasing) {
    auto rms = [](double tone) {
        std::vector<float> input(4800);
        for (size_t i = 0; i < input.size(); ++i)
            input[i] = std::sin(2 * std::numbers::pi * tone * i / 48000);
        opk::PcmRingBuffer ring(2000);
        EXPECT_TRUE(ring.feed(bytesOf<float>(input), opk::AudioSampleType::F32, 1, 48000));
        const auto output = values(ring.view());
        double energy = 0;
        for (size_t i = 100; i < output.size(); ++i)
            energy += output[i] * output[i];
        return std::sqrt(energy / (output.size() - 100));
    };
    EXPECT_NEAR(rms(1000), std::sqrt(0.5), 0.01);
    EXPECT_LT(rms(12000), 0.005);

    opk::PcmRingBuffer dc(2000);
    std::vector<float> constant(4410, 0.5f);
    ASSERT_TRUE(dc.feed(bytesOf<float>(constant), opk::AudioSampleType::F32, 1, 44100));
    const auto output = values(dc.view());
    for (size_t i = 100; i < output.size(); ++i)
        EXPECT_NEAR(output[i], 0.5f, 1e-6);
}

TEST(PcmRingBuffer, FeedRetainsOnlyCapacityAndAcceptsSingleFrameChunks) {
    opk::PcmRingBuffer ring(3), empty(0);
    const float value = 0.5f;
    for (size_t i = 0; i < 300; ++i) {
        auto bytes = bytesOf<float>(std::span(&value, 1));
        ASSERT_TRUE(ring.feed(bytes, opk::AudioSampleType::F32, 1, 48000));
        ASSERT_TRUE(empty.feed(bytes, opk::AudioSampleType::F32, 1, 48000));
    }
    EXPECT_EQ(ring.endPosition(), 100u);
    EXPECT_EQ(ring.beginPosition(), 97u);
    EXPECT_EQ(ring.size(), 3u);
    EXPECT_EQ(empty.endPosition(), 100u);
    EXPECT_TRUE(empty.empty());
    for (float sample : values(ring.view()))
        EXPECT_NEAR(sample, 0.5f, 1e-6);
}

TEST(PcmRingBuffer, FractionalResamplingPreservesTonePhaseAndReportedDelay) {
    for (size_t rate : {8000u, 44100u}) {
        SCOPED_TRACE(rate);
        std::vector<float> input(rate / 10);
        for (size_t i = 0; i < input.size(); ++i)
            input[i] = std::sin(2 * std::numbers::pi * 1000 * i / rate);
        opk::PcmRingBuffer ring(2000);
        ASSERT_TRUE(ring.feed(bytesOf<float>(input), opk::AudioSampleType::F32, 1, rate));
        const auto output = values(ring.view());
        const double delaySeconds = ring.resamplingDelayNs() / 1e9;
        for (size_t i = 100; i < output.size(); ++i) {
            const auto expected =
                std::sin(2 * std::numbers::pi * 1000 * (i / 16000.0 - delaySeconds));
            EXPECT_NEAR(output[i], expected, 0.002);
        }
    }
}

} // namespace
