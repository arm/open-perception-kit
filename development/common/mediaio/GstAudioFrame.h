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
 * @file GstAudioFrame.h
 * @brief GStreamer-backed implementation of the mediaio AudioFrame interface.
 */

#ifndef OPK_MEDIAIO_GST_AUDIO_FRAME_H
#define OPK_MEDIAIO_GST_AUDIO_FRAME_H

#include "mediaio/AudioFrame.h"
#include <gst/audio/audio.h>
#include <memory>
#include <optional>

namespace opk::mediaio::gst {

/**
 * @brief Read-only mapped audio that owns its GstBuffer reference and mapping.
 *
 * Instances are created by mapGstBuffer() and expose original-format interleaved
 * PCM without conversion or resampling. Destruction unmaps the buffer and releases
 * the owned reference, invalidating the borrowed DataView and its data pointer.
 */
class GstAudioFrame final : public AudioFrame {
  public:
    /**
     * @brief Translates a supported GStreamer PCM format to an OPK sample type.
     * @param format GStreamer format: U8, native-endian S16/S32/F32, or packed S24LE.
     * @return Matching sample type, or std::nullopt for an unsupported format.
     */
    static std::optional<AudioSampleType> sampleTypeFromFormat(GstAudioFormat format) noexcept;

    /**
     * @brief Checks whether negotiated audio information describes a supported layout.
     * @param info Audio information to validate.
     * @return True for valid interleaved audio with a supported sample format and
     *         positive bytes per sample frame. This does not map or validate a buffer.
     */
    static bool supports(const GstAudioInfo &info) noexcept;

    /**
     * @brief Maps a PCM buffer for read access using negotiated audio information.
     *
     * Takes its own buffer reference; the caller retains ownership of the supplied
     * reference. The returned frame owns the mapping and exposes it through data().
     * GStreamer may consolidate memory while mapping; no explicit PCM copy is made
     * by this adapter. Missing buffer PTS becomes InvalidTimestampNs.
     *
     * @param buffer Buffer to map.
     * @param info Negotiated format, rate, channels, and layout for the buffer.
     * @return Mapped frame, or nullptr for a null buffer, unsupported information,
     *         incomplete sample frames, or mapping failure.
     */
    static std::unique_ptr<GstAudioFrame> mapGstBuffer(GstBuffer *buffer, const GstAudioInfo &info);

    /// Releases the owned read mapping and buffer reference, when present.
    ~GstAudioFrame() override;

    /// Copying is disabled because the frame owns a buffer reference and mapping.
    GstAudioFrame(const GstAudioFrame &) = delete;

    /// Copy assignment is disabled to prevent duplicated mapping ownership.
    GstAudioFrame &operator=(const GstAudioFrame &) = delete;

    /// Moving is disabled so borrowed DataView references remain stable.
    GstAudioFrame(GstAudioFrame &&) = delete;

    /// Move assignment is disabled to keep mapping ownership unambiguous.
    GstAudioFrame &operator=(GstAudioFrame &&) = delete;

    /** @copydoc opk::mediaio::AudioFrame::sampleType() */
    AudioSampleType sampleType() const noexcept override {
        return type_;
    }
    /** @copydoc opk::mediaio::AudioFrame::channels() */
    size_t channels() const noexcept override {
        return channels_;
    }
    /** @copydoc opk::mediaio::AudioFrame::sampleRate() */
    size_t sampleRate() const noexcept override {
        return rate_;
    }
    /** @copydoc opk::mediaio::AudioFrame::sampleCount() */
    size_t sampleCount() const noexcept override {
        return count_;
    }
    /** @copydoc opk::mediaio::AudioFrame::timestampNs() */
    TimestampNs timestampNs() const noexcept override {
        return timestamp_;
    }
    /** @copydoc opk::mediaio::AudioFrame::data() */
    const DataView &data() const noexcept override {
        return data_;
    }

  private:
    /// Creates an empty owner for initialization by mapGstBuffer().
    GstAudioFrame() = default;
    /// Owned reference to the source GStreamer buffer.
    GstBuffer *buffer_ = nullptr;
    /// Read mapping of buffer_, valid when mapped_ is true.
    GstMapInfo map_ = GST_MAP_INFO_INIT;
    /// Records whether destruction must unmap map_.
    bool mapped_ = false;
    /// Original PCM sample encoding.
    AudioSampleType type_ = AudioSampleType::F32;
    /// Number of interleaved channels.
    size_t channels_ = 0;
    /// Original sample rate in samples per second per channel.
    size_t rate_ = 0;
    /// Number of complete sample frames, equal to samples per channel.
    size_t count_ = 0;
    /// Source presentation timestamp, or InvalidTimestampNs when absent.
    TimestampNs timestamp_ = InvalidTimestampNs;
    /// Borrowed read-only view over map_ storage.
    DataView data_;
};

} // namespace opk::mediaio::gst
#endif // OPK_MEDIAIO_GST_AUDIO_FRAME_H
