/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace pek {
// TODO: Only a skeleton at the moment should be fully reviewed once it's fully implemented

enum class AudioSampleType { U8, S16, S24, S32, F32 };

struct PcmAudioView {
    const float *data0 = nullptr;
    const float *data1 = nullptr;
    size_t sampleCount0 = 0;
    size_t sampleCount1 = 0;

    const size_t channels = 1;
    const size_t frequency = 16000;
    const AudioSampleType sampleType = AudioSampleType::F32;
};

static size_t getAudioSampleByteSize(AudioSampleType t) {
    switch (t) {
    case AudioSampleType::U8:
        return 1;
    case AudioSampleType::S16:
        return 2;
    case AudioSampleType::S24:
        return 3;
    case AudioSampleType::S32:
        return 4;
    case AudioSampleType::F32:
        return 4;
    }
    assert(0);
    throw std::runtime_error("Unknown AudioSampleType");
}

class PcmAudioBuffer {
  public:
    // sampleLimit is in OUTPUT samples (mono, 16 kHz, float32)
    // default is 1 minute of audio, hopefully enough for most networks
    explicit PcmAudioBuffer(size_t sampleLimit = 16000 * 60) : sampleLimit(sampleLimit) {
        channels = 1;
        frequency = 16000;
        data.resize(sampleLimit);
    }

    void getLatestSamples(float *&outBuffer0,
                          size_t &outSampleCount0,
                          float *&outBuffer1,
                          size_t &outSampleCount1,
                          size_t count) const {
        outBuffer0 = nullptr;
        outSampleCount0 = 0;
        outBuffer1 = nullptr;
        outSampleCount1 = 0;

        if (count > filled)
            count = filled;

        if (count == 0)
            return;

        // Oldest sample of requested window
        size_t start = (writePos + sampleLimit - count) % sampleLimit;

        if (start + count <= sampleLimit) {
            // No wrap
            outBuffer0 = const_cast<float *>(&data[start]);
            outSampleCount0 = count;
            return;
        }

        // Wrapped case
        size_t firstPart = sampleLimit - start;
        size_t secondPart = count - firstPart;

        outBuffer0 = const_cast<float *>(&data[start]);
        outSampleCount0 = firstPart;

        outBuffer1 = const_cast<float *>(&data[0]);
        outSampleCount1 = secondPart;
    }

    void getLatestSamples(float *dst, size_t count) const {
        if (count > filled)
            count = filled;

        if (count == 0)
            return;

        // Oldest sample of requested window
        size_t start = (writePos + sampleLimit - count) % sampleLimit;

        if (start + count <= sampleLimit) {
            // No wrap
            std::memcpy(dst, &data[start], count * sizeof(float));
        } else {
            // Wrapped
            size_t firstPart = sampleLimit - start;
            size_t secondPart = count - firstPart;

            std::memcpy(dst, &data[start], firstPart * sizeof(float));
            std::memcpy(dst + firstPart, &data[0], secondPart * sizeof(float));
        }
    }

    PcmAudioView getLatestSamples(size_t count) const {
        PcmAudioView view{};

        if (count > filled)
            count = filled;

        if (count == 0)
            return view;

        size_t start = (writePos + sampleLimit - count) % sampleLimit;

        if (start + count <= sampleLimit) {
            view.data0 = &data[start];
            view.sampleCount0 = count;
            return view;
        }

        size_t firstPart = sampleLimit - start;
        size_t secondPart = count - firstPart;

        view.data0 = &data[start];
        view.sampleCount0 = firstPart;

        view.data1 = &data[0];
        view.sampleCount1 = secondPart;

        return view;
    }

    // inputSampleCount is in FRAMES (i.e. per-channel samples),
    // inputData is interleaved if channels > 1.
    void feed(const uint8_t *inputData,
              AudioSampleType sampleType,
              size_t inputSampleCount,
              size_t inputChannels,
              size_t inputFrequency) {
        if (!inputData || inputSampleCount == 0 || inputChannels == 0 || inputFrequency == 0)
            return;

        const size_t bytesPerSample = getAudioSampleByteSize(sampleType);
        const size_t frameBytes = bytesPerSample * inputChannels;

        // 1) Decode + downmix -> mono float @ inputFrequency
        monoIn.clear();
        monoIn.reserve(inputSampleCount);

        for (size_t i = 0; i < inputSampleCount; ++i) {
            const uint8_t *frame = inputData + i * frameBytes;

            float acc = 0.0f;
            for (size_t ch = 0; ch < inputChannels; ++ch) {
                const uint8_t *s = frame + ch * bytesPerSample;
                acc += decodeToFloat(s, sampleType);
            }
            monoIn.push_back(acc / static_cast<float>(inputChannels));
        }

        // 2) Stateless resample monoIn -> monoOut @ 16 kHz (linear interpolation)
        monoOut.clear();

        if (inputFrequency == 16000) {
            monoOut = monoIn;
        } else if (monoIn.size() == 1) {
            // Degenerate: can't interpolate; just replicate
            monoOut.push_back(monoIn[0]);
        } else {
            // Map output sample indices to fractional input indices
            // ratio = input samples per output sample
            const double ratio = static_cast<double>(inputFrequency) / 16000.0;

            // Estimate output length; clamp to >=1 if input exists
            const size_t outCount = std::max<size_t>(
                1, static_cast<size_t>(std::llround(static_cast<double>(monoIn.size()) / ratio)));

            monoOut.reserve(outCount);

            for (size_t j = 0; j < outCount; ++j) {
                const double srcPos = static_cast<double>(j) * ratio;
                size_t i0 = static_cast<size_t>(srcPos);
                double frac = srcPos - static_cast<double>(i0);

                // Clamp to valid interpolation range [0, size-2]
                if (i0 >= monoIn.size() - 1) {
                    i0 = monoIn.size() - 2;
                    frac = 1.0;
                }

                const float s0 = monoIn[i0];
                const float s1 = monoIn[i0 + 1];
                const float y = static_cast<float>((1.0 - frac) * s0 + frac * s1);
                monoOut.push_back(y);
            }
        }

        // 3) Push into ring buffer
        if (sampleLimit) {
            for (float s : monoOut) {
                data[writePos] = s;
                writePos = (writePos + 1) % sampleLimit;
                filled = std::min(sampleLimit, filled + 1);
            }
        }
    }

  protected:
    // ring buffer state
    size_t sampleLimit = 0;   // capacity in mono output samples (@16k)
    size_t channels = 1;      // fixed output channels
    size_t frequency = 16000; // fixed output frequency
    std::vector<float> data;  // ring storage (mono)
    size_t writePos = 0;      // next write index
    size_t filled = 0;        // number of valid samples in buffer (<= sampleLimit)

    double resampleSrcPos = 0.0; // fractional source position within "work" buffer

    // scratch buffers to avoid reallocs
    std::vector<float> monoIn;  // mono @ inputFrequency
    std::vector<float> monoOut; // mono @ 16k

    static inline float decodeToFloat(const uint8_t *p, AudioSampleType t) {
        // Returns normalized float roughly in [-1, +1].
        switch (t) {
        case AudioSampleType::U8: {
            const int v = static_cast<int>(*p) - 128;
            return static_cast<float>(v) / 128.0f;
        }
        case AudioSampleType::S16: {
            int16_t v;
            std::memcpy(&v, p, sizeof(v));
            return static_cast<float>(v) / 32768.0f;
        }
        case AudioSampleType::S24: {
            int32_t v = (static_cast<int32_t>(p[0])) | (static_cast<int32_t>(p[1]) << 8) |
                        (static_cast<int32_t>(p[2]) << 16);
            if (v & 0x00800000)
                v |= 0xFF000000;
            return static_cast<float>(v) / 8388608.0f; // 2^23
        }
        case AudioSampleType::S32: {
            int32_t v;
            std::memcpy(&v, p, sizeof(v));
            return static_cast<float>(v) / 2147483648.0f; // 2^31
        }
        case AudioSampleType::F32: {
            float v;
            std::memcpy(&v, p, sizeof(v));
            return v;
        }
        }
        return 0.0f;
    }
};

} // namespace pek