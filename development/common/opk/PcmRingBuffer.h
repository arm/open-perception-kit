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

#ifndef OPK_PCM_RING_BUFFER_H
#define OPK_PCM_RING_BUFFER_H

#include "opk/Types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace opk::mediaio {
class AudioFrame;
}

namespace opk {

/// Bounded history of mono Float32 PCM samples at 16000 Hz.
/// feed() converts source PCM; append() accepts samples already in the stored format.
/// All appended samples must belong to one stream.
/// Sample positions count all appended samples, including discarded samples.
/// Not thread-safe. All borrowed views are invalidated by a successful nonempty
/// append(), feed() that emits samples, clear(), or destruction. No allocation occurs during
/// append().
class PcmRingBuffer {
  public:
    static constexpr AudioSampleType sampleType = AudioSampleType::F32;
    static constexpr size_t channels = 1;
    /// Fixed inference processing rate; source PCM may use other rates.
    /// TODO(EXPKITS-1485): Generalize the model processing-rate contract across
    /// conversion, scheduling/time calculations, and audio metadata.
    static constexpr size_t frequency = 16000;

    /// A borrowed chronological window, split only where storage wraps.
    class View {
      public:
        std::span<const float> first() const {
            return first_;
        }
        std::span<const float> second() const {
            return second_;
        }
        size_t size() const {
            return first_.size() + second_.size();
        }
        bool empty() const {
            return size() == 0;
        }
        uint64_t beginPosition() const {
            return begin_;
        }
        uint64_t endPosition() const {
            return begin_ + size();
        }

        /// Copy into non-overlapping storage. An undersized destination returns
        /// false without writing; any excess destination elements remain unchanged.
        bool copyTo(std::span<float> destination) const;

      private:
        friend class PcmRingBuffer;
        View(uint64_t begin, std::span<const float> first, std::span<const float> second)
            : begin_(begin), first_(first), second_(second) {}
        uint64_t begin_;
        std::span<const float> first_;
        std::span<const float> second_;
    };

    /// Allocate capacity samples. Zero capacity is valid: appends advance the
    /// position but retain no samples. Allocation failures propagate normally.
    explicit PcmRingBuffer(size_t capacity);
    ~PcmRingBuffer();
    PcmRingBuffer(const PcmRingBuffer &) = delete;
    PcmRingBuffer &operator=(const PcmRingBuffer &) = delete;
    PcmRingBuffer(PcmRingBuffer &&) = delete;
    PcmRingBuffer &operator=(PcmRingBuffer &&) = delete;

    size_t capacity() const {
        return storage_.size();
    }
    size_t size() const {
        return size_;
    }
    bool empty() const {
        return size_ == 0;
    }
    uint64_t beginPosition() const {
        return end_ - size_;
    }
    uint64_t endPosition() const {
        return end_;
    }

    /// Decode interleaved PCM, average channels, resample, and append mono 16 kHz floats.
    /// S16/S32/F32 use native byte order; packed S24 is little-endian; U8 is unsigned.
    /// Returns false for invalid format/rate/channels, partial frames, non-finite floats,
    /// position overflow, or source-format changes (call clear() before a new stream).
    /// Input rates: 1..384000 Hz. Resampling uses a stateful windowed-sinc filter.
    /// Emits floor(total input frames * 16000 / inputFrequency) samples in total.
    /// A feed may emit no samples. Filter delay is ceil(16 / cutoff) source frames,
    /// where cutoff = 0.9 * min(1, 16000 / inputFrequency); initial history is silence.
    /// At 16000 Hz resampling is bypassed with no delay.
    /// Do not interleave feed() and append() within a stream; clear() before switching.
    /// No end-of-stream tail draining is performed; clear() discards pending filter history.
    /// May allocate scratch storage. Allocation failures propagate normally.
    bool feed(std::span<const uint8_t> input,
              AudioSampleType inputType,
              size_t inputChannels,
              size_t inputFrequency);

    /// Feed original-format PCM from a readable mediaio frame; no input copy is retained.
    bool feed(const mediaio::AudioFrame &frame);

    /// Append samples, retaining only the newest capacity() samples.
    /// Returns false without mutation if positions would overflow uint64_t or
    /// input aliases this ring's storage. Empty input is a successful no-op.
    bool append(std::span<const float> samples);

    /// Discard history and establish a new position origin (zero by default).
    /// Resets source format and resampler state. Does not resize or zero storage; old samples are
    /// no longer exposed.
    void clear(uint64_t nextPosition = 0);

    /// Presentation delay of emitted samples relative to the input clock, rounded
    /// to nanoseconds. Zero for direct 16 kHz input or before the first feed.
    uint64_t resamplingDelayNs() const;

    View view() const;
    /// Return nullopt for overwritten, future, or partially unavailable windows.
    /// Empty windows are valid at every position in [beginPosition(), endPosition()].
    std::optional<View> window(uint64_t begin, size_t count) const;
    std::optional<View> latest(size_t count) const;

    /// Fill destination with the latest samples in chronological order, left-padding
    /// with zeros when history is shorter. Destination size selects the window length.
    /// Returns false without writing if destination exceeds capacity() or aliases
    /// ring storage. Empty destinations succeed. Does not allocate or change history,
    /// sample positions, or resampler state.
    bool copyLatestPadded(std::span<float> destination) const;

  private:
    struct Converter;
    std::unique_ptr<Converter> converter_;
    std::vector<float> storage_;
    size_t write_ = 0;
    size_t size_ = 0;
    uint64_t end_ = 0;
};

} // namespace opk

#endif // OPK_PCM_RING_BUFFER_H
