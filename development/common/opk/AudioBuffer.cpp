/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "opk/AudioBuffer.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

namespace opk {

size_t PcmAudioView::totalSamples() const {
    return sampleCount0 + sampleCount1;
}

std::vector<float> PcmAudioView::flatten() const {
    std::vector<float> out;
    const size_t total = totalSamples();

    if (total == 0)
        return out;

    out.reserve(total);

    if (sampleCount0 > 0 && data0) {
        out.insert(out.end(), data0, data0 + sampleCount0);
    }

    if (sampleCount1 > 0 && data1) {
        out.insert(out.end(), data1, data1 + sampleCount1);
    }

    return out;
}

PcmAudioView PcmAudioView::getLastSamples(size_t k) const {
    PcmAudioView out{}; // metadata defaults match

    const size_t total = sampleCount0 + sampleCount1;
    assert(k <= total && "PcmAudioView::getLastSamples: not enough samples in view");

    assert(k > 0 && "PcmAudioView::getLastSamples: requesting 0 samples doesn't make sense");

    // Logical start index (0..total-1) of the last-k window
    const size_t start = total - k;

    // Entirely inside data1
    if (start >= sampleCount0) {
        const size_t off1 = start - sampleCount0;
        out.data0 = data1 + off1;
        out.sampleCount0 = k;
        return out;
    }

    // Entirely inside data0
    if (start + k <= sampleCount0) {
        out.data0 = data0 + start;
        out.sampleCount0 = k;
        return out;
    }

    // Spans data0 -> data1
    const size_t firstPart = sampleCount0 - start;
    const size_t secondPart = k - firstPart;

    out.data0 = data0 + start;
    out.sampleCount0 = firstPart;

    out.data1 = data1;
    out.sampleCount1 = secondPart;

    return out;
}

void PcmAudioView::copyTo(float *dst, size_t requiredSamples) const {
    assert(dst != nullptr && "PcmAudioView::copyTo: destination pointer is null");

    const size_t total = totalSamples();
    assert(requiredSamples == total &&
           "PcmAudioView::copyTo: requiredSamples does not match view size");

    // Copy first segment
    if (sampleCount0 > 0) {
        assert(data0 != nullptr);
        std::memcpy(dst, data0, sampleCount0 * sizeof(float));
    }

    // Copy second segment (if present)
    if (sampleCount1 > 0) {
        assert(data1 != nullptr);
        std::memcpy(dst + sampleCount0, data1, sampleCount1 * sizeof(float));
    }
}

PcmAudioBuffer::PcmAudioBuffer(size_t sampleLimit) : sampleLimit(sampleLimit) {
    channels = 1;
    frequency = 16000;
    data.assign(sampleLimit, 0.0f);
}

PcmAudioView PcmAudioBuffer::getView() const {
    PcmAudioView view{};

    if (sampleLimit == 0 || data.empty())
        return view;

    // We want a view of the full ring capacity (oldest -> newest).
    // The oldest element in the *capacity window* is the current writePos
    // (because writePos points to the next write, i.e. the oldest slot).
    const size_t count = sampleLimit;
    const size_t start = writePos; // oldest index for the full-capacity view

    if (start + count <= sampleLimit) {
        // This only happens when start==0
        view.data0 = &data[start];
        view.sampleCount0 = count;
        return view;
    }

    // Wrapped (the usual case when start != 0)
    const size_t firstPart = sampleLimit - start;
    const size_t secondPart = count - firstPart; // == start

    view.data0 = &data[start];
    view.sampleCount0 = firstPart;

    view.data1 = &data[0];
    view.sampleCount1 = secondPart;

    return view;
}

PcmAudioView PcmAudioBuffer::feed(const uint8_t *inputData,
                                  AudioSampleType sampleType,
                                  size_t inputSampleCount,
                                  size_t inputChannels,
                                  size_t inputFrequency) {
    PcmAudioView addedView{};

    if (!inputData || inputSampleCount == 0 || inputChannels == 0 || inputFrequency == 0)
        return addedView;

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

    // 3) Push into ring buffer and track the added samples
    const size_t startWritePos = writePos;
    const size_t samplesAdded = monoOut.size();

    if (sampleLimit) {
        for (float s : monoOut) {
            data[writePos] = s;
            writePos = (writePos + 1) % sampleLimit;
            filled = std::min(sampleLimit, filled + 1);
        }
    }

    // 4) Build view of newly added samples
    if (samplesAdded > 0 && sampleLimit > 0) {
        // Check if new samples wrap around the ring buffer
        if (startWritePos + samplesAdded <= sampleLimit) {
            // No wrap: contiguous segment
            addedView.data0 = &data[startWritePos];
            addedView.sampleCount0 = samplesAdded;
        } else {
            // Wrapped: split into two segments
            const size_t firstPart = sampleLimit - startWritePos;
            const size_t secondPart = samplesAdded - firstPart;

            addedView.data0 = &data[startWritePos];
            addedView.sampleCount0 = firstPart;

            addedView.data1 = &data[0];
            addedView.sampleCount1 = secondPart;
        }
    }

    return addedView;
}

float PcmAudioBuffer::decodeToFloat(const uint8_t *p, opk::AudioSampleType t) {
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

} // namespace opk
