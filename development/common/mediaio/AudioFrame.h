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

/**
 * @file AudioFrame.h
 * @brief Backend-neutral interface for original-format interleaved PCM audio.
 */

#ifndef OPK_MEDIAIO_AUDIO_FRAME_H
#define OPK_MEDIAIO_AUDIO_FRAME_H

#include "mediaio/Common.h"

namespace opk::mediaio {

/**
 * @brief Backend-neutral, CPU-readable interleaved PCM in its original format.
 *
 * The frame owns or borrows its storage; data() remains valid for its lifetime.
 * S16/S32/F32 are native-endian, S24 is packed little-endian, and U8 is unsigned.
 * Implementations must expose complete sample frames and readable host memory.
 * This interface describes input audio without downmixing or resampling it.
 */
class AudioFrame {
  public:
    /// Destroys the frame and releases any resources owned by its implementation.
    virtual ~AudioFrame() = default;

    /** @brief Returns the encoding of each PCM sample. */
    virtual AudioSampleType sampleType() const noexcept = 0;

    /** @brief Returns the number of interleaved audio channels. */
    virtual size_t channels() const noexcept = 0;

    /** @brief Returns the source sample rate in samples per second per channel. */
    virtual size_t sampleRate() const noexcept = 0;

    /** @brief Returns the number of samples per channel, not the total across channels. */
    virtual size_t sampleCount() const noexcept = 0;

    /**
     * @brief Returns the presentation timestamp of the first sample in nanoseconds.
     * @return Presentation timestamp, or InvalidTimestampNs when unavailable.
     */
    virtual TimestampNs timestampNs() const noexcept = 0;

    /**
     * @brief Returns the single CPU-readable interleaved PCM plane.
     * @return Borrowed view valid for the frame lifetime; strideBytes is zero
     *         because image row stride does not apply to this audio layout.
     */
    virtual const DataView &data() const noexcept = 0;
};

} // namespace opk::mediaio
#endif // OPK_MEDIAIO_AUDIO_FRAME_H
