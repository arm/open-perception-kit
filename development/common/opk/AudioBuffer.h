/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

#include "opk/Types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace opk {

/**
 * @brief Non-owning two-segment view over mono PCM samples.
 *
 * The view can reference one contiguous region (`data0`) or a wrapped pair
 * of regions (`data0` + `data1`) from a ring buffer.
 */

struct PcmAudioView {
    /// First contiguous sample segment.
    const float *data0 = nullptr;
    /// Optional second contiguous segment (used when wrapped).
    const float *data1 = nullptr;
    /// Sample count in the first segment.
    size_t sampleCount0 = 0;
    /// Sample count in the optional second segment.
    size_t sampleCount1 = 0;

    /// Channel count of this view (mono).
    static constexpr size_t channels = 1;
    /// Sampling frequency of this view (Hz).
    static constexpr size_t frequency = 16000;
    /// Sample storage type of this view.
    static constexpr AudioSampleType sampleType = AudioSampleType::F32;

    /** @brief Returns total samples across both segments. */
    size_t totalSamples() const;

    /**
     * @brief Flattens the two-segment view into a contiguous vector.
     * @return Contiguous copy of all samples in this view.
     */
    std::vector<float> flatten() const;

    /**
     * @brief Returns a view over the last @p k samples.
     * @param k Number of trailing samples requested.
     * @return A view referencing the last-k sample window.
     */
    PcmAudioView getLastSamples(size_t k) const;

    /**
     * @brief Copies all samples from this view into caller-provided memory.
     * @param dst Destination pointer.
     * @param requiredSamples Expected number of copied samples.
     */
    void copyTo(float *dst, size_t requiredSamples) const;
};

/**
 * @brief Mono PCM ring buffer with input decode/downmix/resample helpers.
 *
 * The buffer stores output samples as float32 at 16 kHz, mono.
 */
class PcmAudioBuffer {
  public:
    /**
     * @brief Constructs a PCM ring buffer.
     * @param sampleLimit Buffer capacity in output samples (mono, 16 kHz, float32).
     *
     * Default capacity corresponds to ~1 minute of audio.
     */
    explicit PcmAudioBuffer(size_t sampleLimit = 16000 * 60);

    /**
     * @brief Returns a view over the current ring-buffer window.
     */
    PcmAudioView getView() const;

    /**
     * @brief Feeds interleaved input PCM into the buffer.
     * @param inputData Input bytes.
     * @param sampleType Input sample format.
     * @param inputSampleCount Input frame count (per-channel samples).
     * @param inputChannels Input channel count.
     * @param inputFrequency Input sample rate in Hz.
     * @return View over the newly written sample window(s).
     *
     * Input is downmixed to mono and converted/resampled to float32 @ 16 kHz.
     */
    PcmAudioView feed(const uint8_t *inputData,
                      AudioSampleType sampleType,
                      size_t inputSampleCount,
                      size_t inputChannels,
                      size_t inputFrequency);

  protected:
    /// Ring-buffer capacity in output samples (mono, 16 kHz).
    size_t sampleLimit = 0;
    /// Fixed output channel count.
    size_t channels = 1;
    /// Fixed output sampling frequency in Hz.
    size_t frequency = 16000;
    /// Ring-buffer storage for mono float samples.
    std::vector<float> data;
    /// Next write index in the ring buffer.
    size_t writePos = 0;
    /// Number of valid samples currently stored (<= sampleLimit).
    size_t filled = 0;

    /// Fractional source position within the temporary resampling workspace.
    double resampleSrcPos = 0.0;

    /// Scratch input buffer (mono @ inputFrequency).
    std::vector<float> monoIn;
    /// Scratch output buffer (mono @ 16 kHz).
    std::vector<float> monoOut;

    /**
     * @brief Decodes one input sample into normalized float.
     * @param p Pointer to encoded sample bytes.
     * @param t Encoded sample format.
     * @return Approx. normalized sample in [-1, +1] (format dependent).
     */
    static float decodeToFloat(const uint8_t *p, opk::AudioSampleType t);
};

} // namespace opk