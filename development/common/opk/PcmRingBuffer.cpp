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
#include "mediaio/AudioFrame.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <numbers>

namespace opk {

struct PcmRingBuffer::Converter {
    AudioSampleType type;
    size_t channels;
    size_t rate;
    // Integer phase keeps the output count independent of input chunk boundaries.
    size_t phase = 0;
    size_t radius = 0;
    size_t write = 0;
    size_t coefficientPhase = std::numeric_limits<size_t>::max();
    double cutoff = 1;
    std::vector<float> history;
    std::vector<double> coefficients;
    std::vector<float> mono;
    std::vector<float> output;

    void setup() {
        if (rate == 16000)
            return;
        cutoff = 0.9 * std::min(1.0, 16000.0 / rate);
        radius = static_cast<size_t>(std::ceil(16.0 / cutoff));
        history.resize(2 * radius + 1, 0);
        coefficients.resize(history.size());
    }

    float filteredSample() {
        // A causal Hann-windowed sinc: delay by radius source samples so the
        // complete filter is available. Initial history is silence. Cache the
        // coefficients for integer ratios such as 48 kHz -> 16 kHz.
        if (coefficientPhase != phase) {
            const double fraction = static_cast<double>(phase) / 16000;
            double total = 0;
            for (size_t k = 0; k < coefficients.size(); ++k) {
                const double distance = static_cast<double>(radius) - k + fraction;
                const double x = std::numbers::pi * cutoff * distance;
                const double window =
                    std::abs(distance) < radius
                        ? 0.5 * (1 + std::cos(std::numbers::pi * distance / radius))
                        : 0;
                coefficients[k] = cutoff * (std::abs(x) < 1e-12 ? 1 : std::sin(x) / x) * window;
                total += coefficients[k];
            }
            for (auto &coefficient : coefficients)
                coefficient /= total; // Preserve DC gain for every fractional phase.
            coefficientPhase = phase;
        }
        double sum = 0;
        size_t index = write;
        for (double coefficient : coefficients) {
            index = index ? index - 1 : history.size() - 1;
            sum += coefficient * history[index];
        }
        return static_cast<float>(sum);
    }

    void resample() {
        size_t out = 0;
        for (float sample : mono) {
            history[write] = sample;
            write = (write + 1) % history.size();
            phase += 16000;
            while (phase >= rate) {
                phase -= rate;
                output[out++] = filteredSample();
            }
        }
    }
};

PcmRingBuffer::~PcmRingBuffer() = default;

bool PcmRingBuffer::feed(const mediaio::AudioFrame &frame) {
    const auto &data = frame.data();
    if (!data.canRead() || data.memoryType() != MemoryType::Host ||
        (data.byteSize() && !data.data()))
        return false;
    return feed(std::span(static_cast<const uint8_t *>(data.data()), data.byteSize()),
                frame.sampleType(),
                frame.channels(),
                frame.sampleRate());
}

bool PcmRingBuffer::feed(std::span<const uint8_t> input,
                         AudioSampleType inputType,
                         size_t inputChannels,
                         size_t inputFrequency) {
    size_t bytes = 0;
    switch (inputType) {
    case AudioSampleType::U8:
        bytes = 1;
        break;
    case AudioSampleType::S16:
        bytes = 2;
        break;
    case AudioSampleType::S24:
        bytes = 3;
        break;
    case AudioSampleType::S32:
    case AudioSampleType::F32:
        bytes = 4;
        break;
    default:
        return false;
    }
    if (!inputChannels || !inputFrequency || inputFrequency > 384000 ||
        inputChannels > std::numeric_limits<size_t>::max() / bytes)
        return false;
    const size_t frameBytes = bytes * inputChannels;
    if (input.size() % frameBytes)
        return false;
    if (converter_ && (converter_->type != inputType || converter_->channels != inputChannels ||
                       converter_->rate != inputFrequency))
        return false;
    if (input.empty())
        return true;

    // Validate float samples before changing filter state or history.
    if (inputType == AudioSampleType::F32) {
        for (size_t i = 0; i < input.size(); i += bytes) {
            float value;
            std::memcpy(&value, input.data() + i, sizeof(value));
            if (!std::isfinite(value))
                return false;
        }
    }
    auto pending = converter_ ? nullptr : std::make_unique<Converter>();
    Converter &c = converter_ ? *converter_ : *pending;
    if (pending) {
        c.type = inputType;
        c.channels = inputChannels;
        c.rate = inputFrequency;
        c.setup();
    }
    const size_t frames = input.size() / frameBytes;
    if (frames > (std::numeric_limits<size_t>::max() - c.phase) / frequency)
        return false;
    const size_t outFrames = (frames * frequency + c.phase) / inputFrequency;
    if (outFrames > std::numeric_limits<uint64_t>::max() - end_)
        return false;
    c.mono.resize(frames);
    c.output.resize(outFrames);
    for (size_t i = 0; i < frames; ++i) {
        double sum = 0;
        for (size_t ch = 0; ch < inputChannels; ++ch) {
            const uint8_t *p = input.data() + i * frameBytes + ch * bytes;
            switch (inputType) {
            case AudioSampleType::U8:
                sum += (static_cast<int>(*p) - 128) / 128.0;
                break;
            case AudioSampleType::S16: {
                int16_t v;
                std::memcpy(&v, p, sizeof(v));
                sum += v / 32768.0;
                break;
            }
            case AudioSampleType::S24: {
                int32_t v = static_cast<int32_t>(p[0]) | (static_cast<int32_t>(p[1]) << 8) |
                            (static_cast<int32_t>(p[2]) << 16);
                if (v & 0x800000)
                    v -= 0x1000000;
                sum += v / 8388608.0;
                break;
            }
            case AudioSampleType::S32: {
                int32_t v;
                std::memcpy(&v, p, sizeof(v));
                sum += v / 2147483648.0;
                break;
            }
            case AudioSampleType::F32: {
                float v;
                std::memcpy(&v, p, sizeof(v));
                sum += v;
                break;
            }
            }
        }
        c.mono[i] = static_cast<float>(sum / inputChannels);
    }
    if (inputFrequency != frequency)
        c.resample();
    if (pending)
        converter_ = std::move(pending);
    return append(inputFrequency != frequency ? std::span<const float>(c.output)
                                              : std::span<const float>(c.mono));
}

bool PcmRingBuffer::View::copyTo(std::span<float> destination) const {
    if (destination.size() < size())
        return false;
    std::copy(first_.begin(), first_.end(), destination.begin());
    if (!second_.empty())
        std::copy(second_.begin(), second_.end(), destination.begin() + first_.size());
    return true;
}

PcmRingBuffer::PcmRingBuffer(size_t capacity) : storage_(capacity) {}

bool PcmRingBuffer::append(std::span<const float> samples) {
    if (samples.empty())
        return true;
    if (samples.size() > std::numeric_limits<uint64_t>::max() - end_)
        return false;
    if (!storage_.empty()) {
        // std::less provides a total pointer order even for unrelated allocations.
        const std::less<> less;
        if (less(samples.data(), storage_.data() + capacity()) &&
            less(storage_.data(), samples.data() + samples.size()))
            return false;

        if (samples.size() >= capacity()) {
            const auto tail = samples.last(capacity());
            std::copy(tail.begin(), tail.end(), storage_.begin());
            write_ = 0;
            size_ = capacity();
        } else {
            const size_t firstCount = std::min(samples.size(), capacity() - write_);
            std::copy_n(samples.begin(), firstCount, storage_.begin() + write_);
            const size_t secondCount = samples.size() - firstCount;
            std::copy_n(samples.begin() + firstCount, secondCount, storage_.begin());
            write_ = secondCount ? secondCount : write_ + firstCount;
            if (write_ == capacity())
                write_ = 0;
            size_ += std::min(samples.size(), capacity() - size_);
        }
    }
    end_ += samples.size();
    return true;
}

void PcmRingBuffer::clear(uint64_t nextPosition) {
    converter_.reset();
    write_ = 0;
    size_ = 0;
    end_ = nextPosition;
}

uint64_t PcmRingBuffer::resamplingDelayNs() const {
    if (!converter_ || converter_->rate == frequency)
        return 0;
    // Output j reconstructs input position (j+1)*inputRate/16000 - 1 - radius.
    return ((converter_->radius + 1) * uint64_t{1000000000} + converter_->rate / 2) /
               converter_->rate -
           62500;
}

PcmRingBuffer::View PcmRingBuffer::view() const {
    const auto retained = window(beginPosition(), size_);
    assert(retained.has_value()); // The retained range is always a valid window.
    return *retained;
}

std::optional<PcmRingBuffer::View> PcmRingBuffer::window(uint64_t begin, size_t count) const {
    if (begin < beginPosition() || begin > end_ || count > end_ - begin)
        return std::nullopt;
    if (!count)
        return View(begin, {}, {});
    const size_t oldest = write_ >= size_ ? write_ - size_ : capacity() - (size_ - write_);
    const size_t offset = static_cast<size_t>(begin - beginPosition());
    const size_t start =
        offset >= capacity() - oldest ? offset - (capacity() - oldest) : oldest + offset;
    const size_t firstCount = std::min(count, capacity() - start);
    const std::span<const float> storage(storage_);
    return View(begin, storage.subspan(start, firstCount), storage.first(count - firstCount));
}

std::optional<PcmRingBuffer::View> PcmRingBuffer::latest(size_t count) const {
    if (count > size_)
        return std::nullopt;
    return window(end_ - count, count);
}

bool PcmRingBuffer::copyLatestPadded(std::span<float> destination) const {
    if (destination.size() > capacity())
        return false;
    if (destination.empty())
        return true;
    const std::less<> less;
    if (less(destination.data(), storage_.data() + capacity()) &&
        less(storage_.data(), destination.data() + destination.size()))
        return false;

    const size_t available = std::min(destination.size(), size_);
    const size_t padding = destination.size() - available;
    const auto retained = latest(available);
    assert(retained.has_value()); // available never exceeds the retained sample count.
    std::fill_n(destination.begin(), padding, 0.0f);
    return retained->copyTo(destination.subspan(padding));
}

} // namespace opk
