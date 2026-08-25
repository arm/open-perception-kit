/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

/**
 * @file PixelBufferVideoFrame.cpp
 * @brief Host-memory pixel-buffer VideoFrame implementation.
 */

#include "mediaio/PixelBufferVideoFrame.h"

#include <limits>
#include <utility>

namespace pek::mediaio {

namespace {

/**
 * @brief Returns the tight row stride for a supported image format.
 * @param width Image width in pixels.
 * @param format Pixel layout used to infer bytes per pixel.
 * @return Tight row stride in bytes, or 0 when format is unsupported.
 */
uint32_t defaultStride(uint32_t width, pek::RawImagePixelFormat format) noexcept {
    using enum pek::RawImagePixelFormat;

    switch (format) {
    case Bgra:
        return width * 4;
    case Rgb:
        return width * 3;
    case Gray:
        return width;
    case Yuy2:
        return ((width + 1) / 2) * 4;
    default:
        return 0;
    }
}

/**
 * @brief Returns true when mode is a concrete read/write access mode.
 * @param mode Access mode to validate.
 * @return True when mode is Read, Write, or ReadWrite.
 */
bool validAccessMode(pek::AccessMode mode) noexcept {
    return mode == pek::AccessMode::Read || mode == pek::AccessMode::Write ||
           mode == pek::AccessMode::ReadWrite;
}

/**
 * @brief Returns true when mode permits reading.
 * @param mode Access mode to test.
 * @return True when read access is permitted.
 */
bool canRead(pek::AccessMode mode) noexcept {
    return mode == pek::AccessMode::Read || mode == pek::AccessMode::ReadWrite;
}

/**
 * @brief Returns true when mode permits writing.
 * @param mode Access mode to test.
 * @return True when write access is permitted.
 */
bool canWrite(pek::AccessMode mode) noexcept {
    return mode == pek::AccessMode::Write || mode == pek::AccessMode::ReadWrite;
}

/**
 * @brief Returns true when available access satisfies requested access.
 * @param available Access mode provided by the frame.
 * @param requested Access mode requested by the caller.
 * @return True when available permits all requested operations.
 */
bool accessAllows(pek::AccessMode available, pek::AccessMode requested) noexcept {
    if (!validAccessMode(requested)) {
        return false;
    }
    if (requested == pek::AccessMode::ReadWrite) {
        return canRead(available) && canWrite(available);
    }
    if (requested == pek::AccessMode::Read) {
        return canRead(available);
    }
    return canWrite(available);
}

/**
 * @brief Validates non-zero dimensions and stride multiplication safety.
 * @param width Image width in pixels.
 * @param height Image height in pixels.
 * @param strideBytes Row stride in bytes.
 * @return True when dimensions are non-zero and height * strideBytes fits in size_t.
 */
bool validDimensions(uint32_t width, uint32_t height, uint32_t strideBytes) noexcept {
    if (width == 0 || height == 0 || strideBytes == 0) {
        return false;
    }

    const auto maxSize = std::numeric_limits<size_t>::max();
    return static_cast<size_t>(height) <= maxSize / static_cast<size_t>(strideBytes);
}

/**
 * @brief Validates host pixel-buffer metadata before creating a frame.
 * @param data Pointer to the first byte of the pixel buffer.
 * @param byteSize Number of bytes available from data.
 * @param width Frame width in pixels.
 * @param height Frame height in pixels.
 * @param format Pixel layout represented by the buffer.
 * @param strideBytes Row stride in bytes.
 * @param accessMode Permitted access mode for the buffer.
 * @return True when the buffer metadata is usable for a PixelBufferVideoFrame.
 */
bool validPixelBuffer(const void *data,
                      size_t byteSize,
                      uint32_t width,
                      uint32_t height,
                      pek::RawImagePixelFormat format,
                      uint32_t strideBytes,
                      pek::AccessMode accessMode) noexcept {
    if (const uint32_t minimumStrideBytes = defaultStride(width, format);
        data == nullptr || byteSize == 0 || minimumStrideBytes == 0 ||
        strideBytes < minimumStrideBytes || !validAccessMode(accessMode) ||
        !validDimensions(width, height, strideBytes)) {
        return false;
    }

    const size_t minimumByteSize = static_cast<size_t>(strideBytes) * static_cast<size_t>(height);
    return byteSize >= minimumByteSize;
}

} // namespace

PixelBufferVideoFrame::PixelBufferVideoFrame(
    void *data,
    size_t byteSize,
    uint32_t width,
    uint32_t height,
    pek::RawImagePixelFormat format,
    uint32_t strideBytes,
    pek::AccessMode accessMode,
    TimestampNs timestampNs,
    std::shared_ptr<const void> lifetimeAnchorValue) noexcept
    : frameData(data), frameByteSize(byteSize), frameWidth(width), frameHeight(height),
      frameFormat(format), frameStrideBytes(strideBytes), frameAccess(accessMode),
      frameTimestampNs(timestampNs), lifetimeAnchor(std::move(lifetimeAnchorValue)) {
    framePlanes.push_back(
        DataView::host(frameData, frameByteSize, frameStrideBytes, frameAccess, 0));
}

std::unique_ptr<PixelBufferVideoFrame>
PixelBufferVideoFrame::borrow(void *data,
                              size_t byteSize,
                              uint32_t width,
                              uint32_t height,
                              pek::RawImagePixelFormat format,
                              uint32_t strideBytes,
                              pek::AccessMode accessMode,
                              TimestampNs timestampNs,
                              std::shared_ptr<const void> lifetimeAnchor) {
    if (strideBytes == 0) {
        strideBytes = defaultStride(width, format);
    }
    if (!validPixelBuffer(data, byteSize, width, height, format, strideBytes, accessMode)) {
        return nullptr;
    }

    return std::unique_ptr<PixelBufferVideoFrame>(
        new PixelBufferVideoFrame(data,
                                  byteSize,
                                  width,
                                  height,
                                  format,
                                  strideBytes,
                                  accessMode,
                                  timestampNs,
                                  std::move(lifetimeAnchor)));
}

std::unique_ptr<PixelBufferVideoFrame>
PixelBufferVideoFrame::borrowReadOnly(const void *data,
                                      size_t byteSize,
                                      uint32_t width,
                                      uint32_t height,
                                      pek::RawImagePixelFormat format,
                                      uint32_t strideBytes,
                                      TimestampNs timestampNs,
                                      std::shared_ptr<const void> lifetimeAnchor) {
    return borrow(const_cast<void *>(data),
                  byteSize,
                  width,
                  height,
                  format,
                  strideBytes,
                  pek::AccessMode::Read,
                  timestampNs,
                  std::move(lifetimeAnchor));
}

std::unique_ptr<PixelBufferVideoFrame> PixelBufferVideoFrame::take(std::vector<uint8_t> buffer,
                                                                   uint32_t width,
                                                                   uint32_t height,
                                                                   pek::RawImagePixelFormat format,
                                                                   uint32_t strideBytes,
                                                                   pek::AccessMode accessMode,
                                                                   TimestampNs timestampNs) {
    auto storage = std::make_shared<std::vector<uint8_t>>(std::move(buffer));
    return borrow(storage->data(),
                  storage->size(),
                  width,
                  height,
                  format,
                  strideBytes,
                  accessMode,
                  timestampNs,
                  storage);
}

std::unique_ptr<PixelBufferVideoFrame> PixelBufferVideoFrame::copy(const void *data,
                                                                   size_t byteSize,
                                                                   uint32_t width,
                                                                   uint32_t height,
                                                                   pek::RawImagePixelFormat format,
                                                                   uint32_t strideBytes,
                                                                   pek::AccessMode accessMode,
                                                                   TimestampNs timestampNs) {
    if (data == nullptr || byteSize == 0) {
        return nullptr;
    }

    const auto *begin = static_cast<const uint8_t *>(data);
    std::vector<uint8_t> buffer(begin, begin + byteSize);
    return take(std::move(buffer), width, height, format, strideBytes, accessMode, timestampNs);
}

pek::RawImagePixelFormat PixelBufferVideoFrame::format() const noexcept {
    return frameFormat;
}

pek::YuvColorMatrix PixelBufferVideoFrame::yuvColorMatrix() const noexcept {
    using enum pek::YuvColorMatrix;

    if (frameFormat == pek::RawImagePixelFormat::Yuy2) {
        return frameHeight <= 576 ? Bt601 : Bt709;
    }
    return Unknown;
}

pek::YuvRange PixelBufferVideoFrame::yuvRange() const noexcept {
    using enum pek::YuvRange;

    if (frameFormat == pek::RawImagePixelFormat::Yuy2) {
        return Limited;
    }
    return Unknown;
}

uint32_t PixelBufferVideoFrame::width() const noexcept {
    return frameWidth;
}

uint32_t PixelBufferVideoFrame::height() const noexcept {
    return frameHeight;
}

TimestampNs PixelBufferVideoFrame::timestampNs() const noexcept {
    return frameTimestampNs;
}

pek::MemoryType PixelBufferVideoFrame::memoryType() const noexcept {
    return pek::MemoryType::Host;
}

std::span<const DataView> PixelBufferVideoFrame::planes() const noexcept {
    return framePlanes;
}

bool PixelBufferVideoFrame::canMap(pek::AccessMode mode) const noexcept {
    return frameData != nullptr && accessAllows(frameAccess, mode);
}

std::unique_ptr<VideoFrame> PixelBufferVideoFrame::map(pek::AccessMode mode) const {
    if (!canMap(mode)) {
        return nullptr;
    }

    return borrow(frameData,
                  frameByteSize,
                  frameWidth,
                  frameHeight,
                  frameFormat,
                  frameStrideBytes,
                  mode,
                  frameTimestampNs,
                  lifetimeAnchor);
}

std::unique_ptr<VideoFrame> makePixelBufferVideoFrame(void *data,
                                                      size_t byteSize,
                                                      uint32_t width,
                                                      uint32_t height,
                                                      pek::RawImagePixelFormat format,
                                                      uint32_t strideBytes,
                                                      pek::AccessMode accessMode,
                                                      TimestampNs timestampNs,
                                                      std::shared_ptr<const void> lifetimeAnchor) {
    return PixelBufferVideoFrame::borrow(data,
                                         byteSize,
                                         width,
                                         height,
                                         format,
                                         strideBytes,
                                         accessMode,
                                         timestampNs,
                                         std::move(lifetimeAnchor));
}

std::unique_ptr<VideoFrame>
makeReadOnlyPixelBufferVideoFrame(const void *data,
                                  size_t byteSize,
                                  uint32_t width,
                                  uint32_t height,
                                  pek::RawImagePixelFormat format,
                                  uint32_t strideBytes,
                                  TimestampNs timestampNs,
                                  std::shared_ptr<const void> lifetimeAnchor) {
    return PixelBufferVideoFrame::borrowReadOnly(
        data, byteSize, width, height, format, strideBytes, timestampNs, std::move(lifetimeAnchor));
}

std::unique_ptr<VideoFrame> makeOwnedPixelBufferVideoFrame(std::vector<uint8_t> buffer,
                                                           uint32_t width,
                                                           uint32_t height,
                                                           pek::RawImagePixelFormat format,
                                                           uint32_t strideBytes,
                                                           pek::AccessMode accessMode,
                                                           TimestampNs timestampNs) {
    return PixelBufferVideoFrame::take(
        std::move(buffer), width, height, format, strideBytes, accessMode, timestampNs);
}

std::unique_ptr<VideoFrame> copyPixelBufferVideoFrame(const void *data,
                                                      size_t byteSize,
                                                      uint32_t width,
                                                      uint32_t height,
                                                      pek::RawImagePixelFormat format,
                                                      uint32_t strideBytes,
                                                      pek::AccessMode accessMode,
                                                      TimestampNs timestampNs) {
    return PixelBufferVideoFrame::copy(
        data, byteSize, width, height, format, strideBytes, accessMode, timestampNs);
}

} // namespace pek::mediaio
