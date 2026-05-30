/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

/**
 * @file GstVideoFrame.h
 * @brief GStreamer-backed implementation of the mediaio VideoFrame interface.
 */

#pragma once

#include "mediaio/VideoFrame.h"

#include <gst/gst.h>

#include <memory>
#include <span>
#include <vector>

namespace pek::mediaio::gst {

/**
 * @brief Converts GstMapFlags to the matching PEK access mode.
 * @param flags GStreamer map flags.
 * @return Read, Write, ReadWrite, or Unknown when no read/write flag is set.
 */
pek::AccessMode accessModeFromGstMapFlags(GstMapFlags flags) noexcept;

/**
 * @brief Converts a PEK access mode to GStreamer map flags.
 * @param mode Requested PEK access mode.
 * @return GStreamer map flags. Unknown falls back to GST_MAP_READ.
 */
GstMapFlags gstMapFlagsFromAccessMode(pek::AccessMode mode) noexcept;

/**
 * @brief GStreamer-backed VideoFrame implementation.
 *
 * GstVideoFrame hides GstBuffer ref/unref and GstMapInfo map/unmap handling.
 * Host-backed instances own a GStreamer map until destruction. DMA-BUF-backed
 * instances expose fd metadata and keep the originating buffer alive when one is
 * provided.
 */
class GstVideoFrame final : public pek::mediaio::VideoFrame {
  public:
    /// Copying is disabled because the object owns backend references and maps.
    GstVideoFrame(const GstVideoFrame &) = delete;

    /// Copy assignment is disabled because the object owns backend references and maps.
    GstVideoFrame &operator=(const GstVideoFrame &) = delete;

    /// Moving is disabled so DataView pointers and backend map ownership stay stable.
    GstVideoFrame(GstVideoFrame &&) = delete;

    /// Move assignment is disabled so backend map ownership stays unambiguous.
    GstVideoFrame &operator=(GstVideoFrame &&) = delete;

    /// Releases the owned GstMapInfo and GstBuffer reference, when present.
    ~GstVideoFrame() override;

    /**
     * @brief Wraps an already mapped GstBuffer and takes ownership of the map.
     *
     * The caller must not call gst_buffer_unmap() after a successful call. The
     * returned frame keeps its own GstBuffer reference and releases the map in
     * the destructor.
     *
     * @param buffer Buffer that was mapped.
     * @param map Existing GstMapInfo to take over.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @param format Pixel layout represented by the mapped memory.
     * @param strideBytes Row stride in bytes, or 0 for the default tight stride.
     * @param accessMode Access mode granted by the existing map.
     * @return Wrapped frame, or nullptr when buffer/map is invalid.
     */
    static std::unique_ptr<GstVideoFrame>
    takeMappedBuffer(GstBuffer *buffer,
                     GstMapInfo map,
                     uint32_t width,
                     uint32_t height,
                     pek::DataKind format = pek::DataKind::ImageBgraHwc,
                     uint32_t strideBytes = 0,
                     pek::AccessMode accessMode = pek::AccessMode::ReadWrite);

    /**
     * @brief Maps a GstBuffer and returns a VideoFrame owning that mapping.
     * @param buffer Buffer to map.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @param format Pixel layout represented by the mapped memory.
     * @param strideBytes Row stride in bytes, or 0 for the default tight stride.
     * @param accessMode Requested GStreamer map access.
     * @return Mapped frame, or nullptr if the buffer cannot be mapped.
     */
    static std::unique_ptr<GstVideoFrame>
    mapBuffer(GstBuffer *buffer,
              uint32_t width,
              uint32_t height,
              pek::DataKind format = pek::DataKind::ImageBgraHwc,
              uint32_t strideBytes = 0,
              pek::AccessMode accessMode = pek::AccessMode::Read);

    /**
     * @brief Creates a descriptor-only frame for DMA-BUF backed video memory.
     *
     * The DMA-BUF file descriptor is borrowed. If buffer is non-null, the frame
     * keeps a GstBuffer reference so the borrowed descriptor remains valid for
     * the frame lifetime.
     *
     * @param buffer Optional originating GStreamer buffer.
     * @param fd Borrowed DMA-BUF file descriptor.
     * @param byteSize Number of bytes exposed by the plane.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @param strideBytes Row stride in bytes.
     * @param format Pixel layout represented by the DMA-BUF plane.
     * @param offsetBytes Byte offset of the plane inside the DMA-BUF allocation.
     * @param sync Borrowed synchronization fence metadata.
     * @return DMA-BUF frame descriptor, or nullptr when fd is invalid.
     */
    static std::unique_ptr<GstVideoFrame>
    fromDmaBuf(GstBuffer *buffer,
               int fd,
               size_t byteSize,
               uint32_t width,
               uint32_t height,
               uint32_t strideBytes,
               pek::DataKind format = pek::DataKind::ImageBgraHwc,
               size_t offsetBytes = 0,
               DmaBufSync sync = {});

    /** @copydoc pek::mediaio::VideoFrame::format() */
    pek::DataKind format() const noexcept override;

    /** @copydoc pek::mediaio::VideoFrame::width() */
    uint32_t width() const noexcept override;

    /** @copydoc pek::mediaio::VideoFrame::height() */
    uint32_t height() const noexcept override;

    /** @copydoc pek::mediaio::VideoFrame::timestampNs() */
    TimestampNs timestampNs() const noexcept override;

    /** @copydoc pek::mediaio::VideoFrame::memoryType() */
    pek::MemoryType memoryType() const noexcept override;

    /** @copydoc pek::mediaio::VideoFrame::planes() */
    std::span<const DataView> planes() const noexcept override;

    /** @copydoc pek::mediaio::VideoFrame::canMap() */
    bool canMap(pek::AccessMode mode) const noexcept override;

    /** @copydoc pek::mediaio::VideoFrame::map() */
    std::unique_ptr<pek::mediaio::VideoFrame> map(pek::AccessMode mode) const override;

  private:
    /**
     * @brief Constructs a GStreamer-backed frame from prepared plane views.
     * @param buffer Optional originating buffer to keep alive.
     * @param mapped True when mapInfo must be released with gst_buffer_unmap().
     * @param map Existing map metadata owned by this frame when mapped is true.
     * @param memoryType Backing memory type exposed by the frame.
     * @param format Pixel layout represented by the frame.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @param timestampNs Presentation timestamp in nanoseconds.
     * @param planes Prepared plane views exposed by the frame.
     * @param mappedAccessMode Access mode granted by the owned map.
     */
    GstVideoFrame(GstBuffer *buffer,
                  bool mapped,
                  GstMapInfo map,
                  pek::MemoryType memoryType,
                  pek::DataKind format,
                  uint32_t width,
                  uint32_t height,
                  TimestampNs timestampNs,
                  std::vector<DataView> planes,
                  pek::AccessMode mappedAccessMode) noexcept;

    /// Referenced GStreamer buffer kept alive for this frame lifetime.
    GstBuffer *buffer = nullptr;
    /// True when mapInfo is owned by this frame and must be unmapped.
    bool ownsMap = false;
    /// Owned GStreamer map information for host-backed mapped frames.
    GstMapInfo mapInfo{};

    /// Backing memory type exposed by this frame.
    pek::MemoryType frameMemoryType = pek::MemoryType::Unknown;
    /// Pixel layout represented by this frame.
    pek::DataKind frameFormat = pek::DataKind::Unknown;
    /// Frame width in pixels.
    uint32_t frameWidth = 0;
    /// Frame height in pixels.
    uint32_t frameHeight = 0;
    /// Presentation timestamp in nanoseconds.
    TimestampNs frameTimestampNs;
    /// Plane views exposed by this frame.
    std::vector<DataView> framePlanes;
    /// Access mode of the owned host map, or Unknown when not mapped.
    pek::AccessMode mappedAccess = pek::AccessMode::Unknown;
};

/**
 * @brief Takes ownership of an existing GstMapInfo and returns a VideoFrame.
 *
 * After passing a map to this function, the caller must not call
 * gst_buffer_unmap() for it. The returned VideoFrame releases it.
 *
 * @param buffer Buffer that was mapped.
 * @param map Existing GstMapInfo to take over.
 * @param width Frame width in pixels.
 * @param height Frame height in pixels.
 * @param format Pixel layout represented by the mapped memory.
 * @param strideBytes Row stride in bytes, or 0 for the default tight stride.
 * @param accessMode Access mode granted by the existing map.
 * @return Wrapped frame, or nullptr when buffer/map is invalid.
 */
std::unique_ptr<pek::mediaio::VideoFrame>
makeVideoFrameFromMappedBuffer(GstBuffer *buffer,
                               GstMapInfo map,
                               uint32_t width,
                               uint32_t height,
                               pek::DataKind format = pek::DataKind::ImageBgraHwc,
                               uint32_t strideBytes = 0,
                               pek::AccessMode accessMode = pek::AccessMode::ReadWrite);

/**
 * @brief Maps a GstBuffer and returns a VideoFrame owning that mapping.
 * @param buffer Buffer to map.
 * @param width Frame width in pixels.
 * @param height Frame height in pixels.
 * @param format Pixel layout represented by the mapped memory.
 * @param strideBytes Row stride in bytes, or 0 for the default tight stride.
 * @param accessMode Requested GStreamer map access.
 * @return Mapped frame, or nullptr if the buffer cannot be mapped.
 */
std::unique_ptr<pek::mediaio::VideoFrame>
mapVideoFrame(GstBuffer *buffer,
              uint32_t width,
              uint32_t height,
              pek::DataKind format = pek::DataKind::ImageBgraHwc,
              uint32_t strideBytes = 0,
              pek::AccessMode accessMode = pek::AccessMode::Read);

/**
 * @brief Creates a DMA-BUF-backed VideoFrame descriptor.
 *
 * The fd is borrowed. If @p buffer is non-null, the VideoFrame keeps a ref to it
 * so the borrowed fd remains valid for the VideoFrame lifetime.
 *
 * @param buffer Optional originating GStreamer buffer.
 * @param fd Borrowed DMA-BUF file descriptor.
 * @param byteSize Number of bytes exposed by the plane.
 * @param width Frame width in pixels.
 * @param height Frame height in pixels.
 * @param strideBytes Row stride in bytes.
 * @param format Pixel layout represented by the DMA-BUF plane.
 * @param offsetBytes Byte offset of the plane inside the DMA-BUF allocation.
 * @param sync Borrowed synchronization fence metadata.
 * @return DMA-BUF frame descriptor, or nullptr when fd is invalid.
 */
std::unique_ptr<pek::mediaio::VideoFrame>
makeDmaBufVideoFrame(GstBuffer *buffer,
                     int fd,
                     size_t byteSize,
                     uint32_t width,
                     uint32_t height,
                     uint32_t strideBytes,
                     pek::DataKind format = pek::DataKind::ImageBgraHwc,
                     size_t offsetBytes = 0,
                     DmaBufSync sync = {});

} // namespace pek::mediaio::gst
