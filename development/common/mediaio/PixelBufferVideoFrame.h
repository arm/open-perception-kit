/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

/**
 * @file PixelBufferVideoFrame.h
 * @brief Host-memory VideoFrame implementation for raw pixel buffers.
 */

#pragma once

#include "mediaio/VideoFrame.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace pek::mediaio {

/**
 * @brief Host-memory VideoFrame implementation for ordinary pixel buffers.
 *
 * This implementation is intended for pixels loaded from files, tests, or other
 * non-streaming sources. It can either borrow caller-owned memory or own a
 * moved/copied byte buffer.
 */
class PixelBufferVideoFrame final : public VideoFrame {
  public:
    /// Copying is disabled because DataView objects point into this frame's buffer.
    PixelBufferVideoFrame(const PixelBufferVideoFrame &) = delete;

    /// Copy assignment is disabled because DataView objects point into this frame's buffer.
    PixelBufferVideoFrame &operator=(const PixelBufferVideoFrame &) = delete;

    /// Moving is disabled so DataView pointers stay stable.
    PixelBufferVideoFrame(PixelBufferVideoFrame &&) = delete;

    /// Move assignment is disabled so DataView pointers stay stable.
    PixelBufferVideoFrame &operator=(PixelBufferVideoFrame &&) = delete;

    /// Destroys the frame and releases the optional lifetime anchor.
    ~PixelBufferVideoFrame() override = default;

    /**
     * @brief Creates a frame that borrows mutable host pixel memory.
     *
     * The caller keeps ownership of data. Pass lifetimeAnchor when another object must
     * be kept alive for the returned frame and any mapped aliases.
     *
     * @param data Pointer to the first byte of the pixel buffer.
     * @param byteSize Number of bytes available from data.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @param format Pixel layout represented by the buffer.
     * @param strideBytes Row stride in bytes, or 0 for the default tight stride.
     * @param accessMode Permitted read/write access for the buffer.
     * @param timestampNs Optional presentation timestamp in nanoseconds.
     * @param lifetimeAnchor Optional shared owner that keeps borrowed memory valid.
     * @return Pixel-buffer frame, or nullptr when arguments are invalid.
     */
    static std::unique_ptr<PixelBufferVideoFrame>
    borrow(void *data,
           size_t byteSize,
           uint32_t width,
           uint32_t height,
           pek::DataKind format = pek::DataKind::ImageRgbHwc,
           uint32_t strideBytes = 0,
           pek::AccessMode accessMode = pek::AccessMode::ReadWrite,
           TimestampNs timestampNs = InvalidTimestampNs,
           std::shared_ptr<const void> lifetimeAnchor = {});

    /**
     * @brief Creates a frame that borrows read-only host pixel memory.
     *
     * The returned DataView exposes no mutableData(). Pass lifetimeAnchor when another
     * object must be kept alive for the returned frame and any mapped aliases.
     *
     * @param data Pointer to the first byte of the pixel buffer.
     * @param byteSize Number of bytes available from data.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @param format Pixel layout represented by the buffer.
     * @param strideBytes Row stride in bytes, or 0 for the default tight stride.
     * @param timestampNs Optional presentation timestamp in nanoseconds.
     * @param lifetimeAnchor Optional shared owner that keeps borrowed memory valid.
     * @return Pixel-buffer frame, or nullptr when arguments are invalid.
     */
    static std::unique_ptr<PixelBufferVideoFrame>
    borrowReadOnly(const void *data,
                   size_t byteSize,
                   uint32_t width,
                   uint32_t height,
                   pek::DataKind format = pek::DataKind::ImageRgbHwc,
                   uint32_t strideBytes = 0,
                   TimestampNs timestampNs = InvalidTimestampNs,
                   std::shared_ptr<const void> lifetimeAnchor = {});

    /**
     * @brief Creates a frame that owns a moved byte buffer.
     *
     * The vector storage is retained through an internal shared lifetime anchor,
     * so mapped aliases can safely refer to the same memory.
     *
     * @param buffer Pixel bytes to take ownership of.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @param format Pixel layout represented by the buffer.
     * @param strideBytes Row stride in bytes, or 0 for the default tight stride.
     * @param accessMode Permitted read/write access for the owned buffer.
     * @param timestampNs Optional presentation timestamp in nanoseconds.
     * @return Pixel-buffer frame, or nullptr when arguments are invalid.
     */
    static std::unique_ptr<PixelBufferVideoFrame>
    take(std::vector<uint8_t> buffer,
         uint32_t width,
         uint32_t height,
         pek::DataKind format = pek::DataKind::ImageRgbHwc,
         uint32_t strideBytes = 0,
         pek::AccessMode accessMode = pek::AccessMode::ReadWrite,
         TimestampNs timestampNs = InvalidTimestampNs);

    /**
     * @brief Copies pixel bytes and creates a frame that owns the copy.
     * @param data Pointer to bytes to copy.
     * @param byteSize Number of bytes to copy.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @param format Pixel layout represented by the copied buffer.
     * @param strideBytes Row stride in bytes, or 0 for the default tight stride.
     * @param accessMode Permitted read/write access for the owned copy.
     * @param timestampNs Optional presentation timestamp in nanoseconds.
     * @return Pixel-buffer frame, or nullptr when arguments are invalid.
     */
    static std::unique_ptr<PixelBufferVideoFrame>
    copy(const void *data,
         size_t byteSize,
         uint32_t width,
         uint32_t height,
         pek::DataKind format = pek::DataKind::ImageRgbHwc,
         uint32_t strideBytes = 0,
         pek::AccessMode accessMode = pek::AccessMode::ReadWrite,
         TimestampNs timestampNs = InvalidTimestampNs);

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
    std::unique_ptr<VideoFrame> map(pek::AccessMode mode) const override;

  private:
    /**
     * @brief Constructs a host-memory frame from a validated pixel buffer.
     * @param data Pointer to the first byte of the pixel buffer.
     * @param byteSize Number of bytes available from data.
     * @param width Frame width in pixels.
     * @param height Frame height in pixels.
     * @param format Pixel layout represented by the buffer.
     * @param strideBytes Row stride in bytes.
     * @param accessMode Permitted read/write access for the buffer.
     * @param timestampNs Presentation timestamp in nanoseconds.
     * @param lifetimeAnchor Optional shared owner that keeps borrowed memory valid.
     */
    PixelBufferVideoFrame(void *data,
                          size_t byteSize,
                          uint32_t width,
                          uint32_t height,
                          pek::DataKind format,
                          uint32_t strideBytes,
                          pek::AccessMode accessMode,
                          TimestampNs timestampNs,
                          std::shared_ptr<const void> lifetimeAnchorValue) noexcept;

    /// Pointer to the first byte of host pixel data.
    void *frameData = nullptr;
    /// Total number of bytes available from frameData.
    size_t frameByteSize = 0;
    /// Frame width in pixels.
    uint32_t frameWidth = 0;
    /// Frame height in pixels.
    uint32_t frameHeight = 0;
    /// Pixel layout represented by this frame.
    pek::DataKind frameFormat = pek::DataKind::Unknown;
    /// Row stride in bytes.
    uint32_t frameStrideBytes = 0;
    /// Permitted access mode for frameData.
    pek::AccessMode frameAccess = pek::AccessMode::Unknown;
    /// Presentation timestamp in nanoseconds.
    TimestampNs frameTimestampNs;
    /// Optional shared owner that keeps borrowed memory valid.
    std::shared_ptr<const void> lifetimeAnchor;
    /// Plane views exposed by this frame.
    std::vector<DataView> framePlanes;
};

/**
 * @brief Creates a VideoFrame that borrows mutable host pixel memory.
 * @param data Pointer to the first byte of the pixel buffer.
 * @param byteSize Number of bytes available from data.
 * @param width Frame width in pixels.
 * @param height Frame height in pixels.
 * @param format Pixel layout represented by the buffer.
 * @param strideBytes Row stride in bytes, or 0 for the default tight stride.
 * @param accessMode Permitted read/write access for the buffer.
 * @param timestampNs Optional presentation timestamp in nanoseconds.
 * @param lifetimeAnchor Optional shared owner that keeps borrowed memory valid.
 * @return Pixel-buffer frame, or nullptr when arguments are invalid.
 */
std::unique_ptr<VideoFrame>
makePixelBufferVideoFrame(void *data,
                          size_t byteSize,
                          uint32_t width,
                          uint32_t height,
                          pek::DataKind format = pek::DataKind::ImageRgbHwc,
                          uint32_t strideBytes = 0,
                          pek::AccessMode accessMode = pek::AccessMode::ReadWrite,
                          TimestampNs timestampNs = InvalidTimestampNs,
                          std::shared_ptr<const void> lifetimeAnchor = {});

/**
 * @brief Creates a VideoFrame that borrows read-only host pixel memory.
 * @param data Pointer to the first byte of the pixel buffer.
 * @param byteSize Number of bytes available from data.
 * @param width Frame width in pixels.
 * @param height Frame height in pixels.
 * @param format Pixel layout represented by the buffer.
 * @param strideBytes Row stride in bytes, or 0 for the default tight stride.
 * @param timestampNs Optional presentation timestamp in nanoseconds.
 * @param lifetimeAnchor Optional shared owner that keeps borrowed memory valid.
 * @return Pixel-buffer frame, or nullptr when arguments are invalid.
 */
std::unique_ptr<VideoFrame>
makeReadOnlyPixelBufferVideoFrame(const void *data,
                                  size_t byteSize,
                                  uint32_t width,
                                  uint32_t height,
                                  pek::DataKind format = pek::DataKind::ImageRgbHwc,
                                  uint32_t strideBytes = 0,
                                  TimestampNs timestampNs = InvalidTimestampNs,
                                  std::shared_ptr<const void> lifetimeAnchor = {});

/**
 * @brief Creates a VideoFrame that owns a moved byte buffer.
 * @param buffer Pixel bytes to take ownership of.
 * @param width Frame width in pixels.
 * @param height Frame height in pixels.
 * @param format Pixel layout represented by the buffer.
 * @param strideBytes Row stride in bytes, or 0 for the default tight stride.
 * @param accessMode Permitted read/write access for the owned buffer.
 * @param timestampNs Optional presentation timestamp in nanoseconds.
 * @return Pixel-buffer frame, or nullptr when arguments are invalid.
 */
std::unique_ptr<VideoFrame>
makeOwnedPixelBufferVideoFrame(std::vector<uint8_t> buffer,
                               uint32_t width,
                               uint32_t height,
                               pek::DataKind format = pek::DataKind::ImageRgbHwc,
                               uint32_t strideBytes = 0,
                               pek::AccessMode accessMode = pek::AccessMode::ReadWrite,
                               TimestampNs timestampNs = InvalidTimestampNs);

/**
 * @brief Copies pixel bytes and creates a VideoFrame that owns the copy.
 * @param data Pointer to bytes to copy.
 * @param byteSize Number of bytes to copy.
 * @param width Frame width in pixels.
 * @param height Frame height in pixels.
 * @param format Pixel layout represented by the copied buffer.
 * @param strideBytes Row stride in bytes, or 0 for the default tight stride.
 * @param accessMode Permitted read/write access for the owned copy.
 * @param timestampNs Optional presentation timestamp in nanoseconds.
 * @return Pixel-buffer frame, or nullptr when arguments are invalid.
 */
std::unique_ptr<VideoFrame>
copyPixelBufferVideoFrame(const void *data,
                          size_t byteSize,
                          uint32_t width,
                          uint32_t height,
                          pek::DataKind format = pek::DataKind::ImageRgbHwc,
                          uint32_t strideBytes = 0,
                          pek::AccessMode accessMode = pek::AccessMode::ReadWrite,
                          TimestampNs timestampNs = InvalidTimestampNs);

} // namespace pek::mediaio
