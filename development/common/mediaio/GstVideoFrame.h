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

/**
 * @file GstVideoFrame.h
 * @brief GStreamer-backed implementation of the mediaio VideoFrame interface.
 */

#pragma once

#include "mediaio/VideoFrame.h"

#include <gst/gst.h>
#include <gst/video/video.h>

#include <memory>
#include <span>
#include <vector>

namespace opk::mediaio::gst {

/**
 * @brief Converts GstMapFlags to the matching OPK access mode.
 * @param flags GStreamer map flags.
 * @return Read, Write, ReadWrite, or Unknown when no read/write flag is set.
 */
opk::AccessMode accessModeFromGstMapFlags(GstMapFlags flags) noexcept;

/**
 * @brief Converts an OPK access mode to GStreamer map flags.
 * @param mode Requested OPK access mode.
 * @return GStreamer map flags. Unknown falls back to GST_MAP_READ.
 */
GstMapFlags gstMapFlagsFromAccessMode(opk::AccessMode mode) noexcept;

/**
 * @brief GStreamer-backed VideoFrame implementation.
 *
 * GstVideoFrame hides GstBuffer ref/unref and GStreamer GstVideoFrame
 * map/unmap handling. Instances are always CPU-mapped frames returned by
 * mapGstBuffer().
 */
class GstVideoFrame final : public opk::mediaio::VideoFrame {
  public:
    /// Copying is disabled because the object owns backend references and maps.
    GstVideoFrame(const GstVideoFrame &) = delete;

    /// Copy assignment is disabled because the object owns backend references and maps.
    GstVideoFrame &operator=(const GstVideoFrame &) = delete;

    /// Moving is disabled so DataView pointers and backend map ownership stay stable.
    GstVideoFrame(GstVideoFrame &&) = delete;

    /// Move assignment is disabled so backend map ownership stays unambiguous.
    GstVideoFrame &operator=(GstVideoFrame &&) = delete;

    /// Releases the owned GStreamer map and GstBuffer reference, when present.
    ~GstVideoFrame() override;

    /**
     * @brief Returns true when any GstMemory in @p buffer is DMA-BUF-backed.
     */
    static bool hasDmaBufContent(GstBuffer *buffer) noexcept;

    /**
     * @brief Returns true when @p buffer is backed by plain system memory.
     *
     * This is a non-mapping type check intended for routing decisions before
     * mapGstBuffer() is attempted. It does not guarantee a future map cannot
     * fail due to locking or access-mode constraints.
     */
    static bool hasDirectCpuAddress(GstBuffer *buffer) noexcept;

    /**
     * @brief Maps a video GstBuffer using negotiated GstVideoInfo.
     *
     * The returned frame owns a GstBuffer reference and the GStreamer video map.
     * The map is released with gst_video_frame_unmap() and the buffer reference
     * with gst_buffer_unref() when the shared frame is destroyed.
     *
     * @param buffer Buffer to map.
     * @param videoInfo Negotiated video layout for the buffer.
     * @param accessMode Requested GStreamer map access.
     * @return Mapped frame, or nullptr if the buffer cannot be mapped.
     */
    static std::shared_ptr<GstVideoFrame>
    mapGstBuffer(GstBuffer *buffer,
                 const GstVideoInfo &videoInfo,
                 opk::AccessMode accessMode = opk::AccessMode::Read);

    /** @copydoc opk::mediaio::VideoFrame::format() */
    opk::RawImagePixelFormat format() const noexcept override;

    /** @copydoc opk::mediaio::VideoFrame::yuvColorMatrix() */
    opk::YuvColorMatrix yuvColorMatrix() const noexcept override;

    /** @copydoc opk::mediaio::VideoFrame::yuvRange() */
    opk::YuvRange yuvRange() const noexcept override;

    /** @copydoc opk::mediaio::VideoFrame::width() */
    uint32_t width() const noexcept override;

    /** @copydoc opk::mediaio::VideoFrame::height() */
    uint32_t height() const noexcept override;

    /** @copydoc opk::mediaio::VideoFrame::timestampNs() */
    TimestampNs timestampNs() const noexcept override;

    /** @copydoc opk::mediaio::VideoFrame::memoryType() */
    opk::MemoryType memoryType() const noexcept override;

    /** @copydoc opk::mediaio::VideoFrame::planes() */
    std::span<const DataView> planes() const noexcept override;

    /** @copydoc opk::mediaio::VideoFrame::canMap() */
    bool canMap(opk::AccessMode mode) const noexcept override;

    /** @copydoc opk::mediaio::VideoFrame::map() */
    std::unique_ptr<opk::mediaio::VideoFrame> map(opk::AccessMode mode) const override;

  private:
    /**
     * @brief Constructs a GStreamer-backed frame from prepared plane views.
     * @param buffer Originating buffer to keep alive.
     * @param frameMap Existing video-frame map owned by this frame.
     * @param videoInfo Video layout used for remapping this buffer.
     * @param memoryType Backing memory type exposed by the frame.
     * @param format Pixel layout represented by the frame.
     * @param yuvMatrix YUV-to-RGB matrix for YUV frames.
     * @param yuvRange Encoded YUV sample range for YUV frames.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @param timestampNs Presentation timestamp in nanoseconds.
     * @param planes Prepared plane views exposed by the frame.
     * @param mappedAccessMode Access mode granted by the owned map.
     */
    GstVideoFrame(GstBuffer *buffer,
                  ::GstVideoFrame frameMap,
                  GstVideoInfo videoInfo,
                  opk::MemoryType memoryType,
                  opk::RawImagePixelFormat format,
                  opk::YuvColorMatrix yuvMatrix,
                  opk::YuvRange yuvRange,
                  uint32_t width,
                  uint32_t height,
                  TimestampNs timestampNs,
                  std::vector<DataView> planes,
                  opk::AccessMode mappedAccessMode) noexcept;

    static std::unique_ptr<GstVideoFrame> mapGstBufferUnique(GstBuffer *buffer,
                                                             const GstVideoInfo &videoInfo,
                                                             opk::AccessMode accessMode);

    /// Referenced GStreamer buffer kept alive for this frame lifetime.
    GstBuffer *buffer = nullptr;
    /// Owned GStreamer video-frame map information for video mapped frames.
    ::GstVideoFrame videoFrameMap{};
    /// Video layout used when remapping the originating GstBuffer.
    GstVideoInfo frameVideoInfo{};

    /// Backing memory type exposed by this frame.
    opk::MemoryType frameMemoryType = opk::MemoryType::Unknown;
    /// Pixel layout represented by this frame.
    opk::RawImagePixelFormat frameFormat = opk::RawImagePixelFormat::Unknown;
    /// YUV-to-RGB matrix for YUV frames.
    opk::YuvColorMatrix frameYuvMatrix = opk::YuvColorMatrix::Unknown;
    /// Encoded YUV sample range for YUV frames.
    opk::YuvRange frameYuvRange = opk::YuvRange::Unknown;
    /// Frame width in pixels.
    uint32_t frameWidth = 0;
    /// Frame height in pixels.
    uint32_t frameHeight = 0;
    /// Presentation timestamp in nanoseconds.
    TimestampNs frameTimestampNs;
    /// Plane views exposed by this frame.
    std::vector<DataView> framePlanes;
    /// Access mode of the owned host map, or Unknown when not mapped.
    opk::AccessMode mappedAccess = opk::AccessMode::Unknown;
};

} // namespace opk::mediaio::gst
