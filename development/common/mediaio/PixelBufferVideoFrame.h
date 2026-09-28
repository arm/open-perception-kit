/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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

namespace opk::mediaio {

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
           opk::RawImagePixelFormat format = opk::RawImagePixelFormat::Rgb,
           uint32_t strideBytes = 0,
           opk::AccessMode accessMode = opk::AccessMode::ReadWrite,
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
                   opk::RawImagePixelFormat format = opk::RawImagePixelFormat::Rgb,
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
         opk::RawImagePixelFormat format = opk::RawImagePixelFormat::Rgb,
         uint32_t strideBytes = 0,
         opk::AccessMode accessMode = opk::AccessMode::ReadWrite,
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
         opk::RawImagePixelFormat format = opk::RawImagePixelFormat::Rgb,
         uint32_t strideBytes = 0,
         opk::AccessMode accessMode = opk::AccessMode::ReadWrite,
         TimestampNs timestampNs = InvalidTimestampNs);

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
    std::unique_ptr<VideoFrame> map(opk::AccessMode mode) const override;

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
                          opk::RawImagePixelFormat format,
                          uint32_t strideBytes,
                          opk::AccessMode accessMode,
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
    opk::RawImagePixelFormat frameFormat = opk::RawImagePixelFormat::Unknown;
    /// Row stride in bytes.
    uint32_t frameStrideBytes = 0;
    /// Permitted access mode for frameData.
    opk::AccessMode frameAccess = opk::AccessMode::Unknown;
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
                          opk::RawImagePixelFormat format = opk::RawImagePixelFormat::Rgb,
                          uint32_t strideBytes = 0,
                          opk::AccessMode accessMode = opk::AccessMode::ReadWrite,
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
                                  opk::RawImagePixelFormat format = opk::RawImagePixelFormat::Rgb,
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
                               opk::RawImagePixelFormat format = opk::RawImagePixelFormat::Rgb,
                               uint32_t strideBytes = 0,
                               opk::AccessMode accessMode = opk::AccessMode::ReadWrite,
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
                          opk::RawImagePixelFormat format = opk::RawImagePixelFormat::Rgb,
                          uint32_t strideBytes = 0,
                          opk::AccessMode accessMode = opk::AccessMode::ReadWrite,
                          TimestampNs timestampNs = InvalidTimestampNs);

} // namespace opk::mediaio
