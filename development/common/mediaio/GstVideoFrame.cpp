/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

/**
 * @file GstVideoFrame.cpp
 * @brief GStreamer-backed mediaio VideoFrame implementation.
 */

#include "mediaio/GstVideoFrame.h"

#include <utility>

namespace pek::mediaio::gst {

namespace {

/**
 * @brief Returns the tight row stride for a supported image format.
 * @param width Image width in pixels.
 * @param format Pixel layout used to infer bytes per pixel.
 * @return Tight row stride in bytes, or 0 when format is unsupported.
 */
uint32_t defaultStride(uint32_t width, pek::DataKind format) {
    switch (format) {
    case pek::DataKind::ImageBgraHwc:
        return width * 4;
    case pek::DataKind::ImageRgbHwc:
        return width * 3;
    case pek::DataKind::ImageGray:
        return width;
    default:
        return 0;
    }
}

/**
 * @brief Returns true when mode can be represented as a GStreamer map mode.
 * @param mode PEK access mode to validate.
 * @return True when mode is Read, Write, or ReadWrite.
 */
bool validMapMode(pek::AccessMode mode) noexcept {
    return mode == pek::AccessMode::Read || mode == pek::AccessMode::Write ||
           mode == pek::AccessMode::ReadWrite;
}

/**
 * @brief Extracts GST_BUFFER_PTS as a nanosecond timestamp.
 * @param buffer Buffer whose PTS should be read.
 * @return PTS in nanoseconds, or InvalidTimestampNs when no valid PTS exists.
 */
TimestampNs timestampNsFromGstBuffer(GstBuffer *buffer) noexcept {
    if (buffer == nullptr || !GST_CLOCK_TIME_IS_VALID(GST_BUFFER_PTS(buffer))) {
        return InvalidTimestampNs;
    }
    return static_cast<TimestampNs>(GST_BUFFER_PTS(buffer));
}

} // namespace

pek::AccessMode accessModeFromGstMapFlags(GstMapFlags flags) noexcept {
    const bool read = (flags & GST_MAP_READ) != 0;
    const bool write = (flags & GST_MAP_WRITE) != 0;

    if (read && write) {
        return pek::AccessMode::ReadWrite;
    }
    if (write) {
        return pek::AccessMode::Write;
    }
    if (read) {
        return pek::AccessMode::Read;
    }
    return pek::AccessMode::Unknown;
}

GstMapFlags gstMapFlagsFromAccessMode(pek::AccessMode mode) noexcept {
    switch (mode) {
    case pek::AccessMode::Read:
        return GST_MAP_READ;
    case pek::AccessMode::Write:
        return GST_MAP_WRITE;
    case pek::AccessMode::ReadWrite:
        return static_cast<GstMapFlags>(GST_MAP_READ | GST_MAP_WRITE);
    case pek::AccessMode::Unknown:
        break;
    }
    return GST_MAP_READ;
}

GstVideoFrame::GstVideoFrame(GstBuffer *buffer,
                             bool mapped,
                             GstMapInfo map,
                             pek::MemoryType memoryType,
                             pek::DataKind format,
                             uint32_t width,
                             uint32_t height,
                             TimestampNs timestampNs,
                             std::vector<DataView> planes,
                             pek::AccessMode mappedAccessMode) noexcept
    : buffer(buffer != nullptr ? gst_buffer_ref(buffer) : nullptr), ownsMap(mapped), mapInfo(map),
      frameMemoryType(memoryType), frameFormat(format), frameWidth(width), frameHeight(height),
      frameTimestampNs(timestampNs), framePlanes(std::move(planes)),
      mappedAccess(mappedAccessMode) {}

GstVideoFrame::~GstVideoFrame() {
    if (ownsMap && buffer != nullptr) {
        gst_buffer_unmap(buffer, &mapInfo);
    }
    if (buffer != nullptr) {
        gst_buffer_unref(buffer);
    }
}

std::unique_ptr<GstVideoFrame> GstVideoFrame::takeMappedBuffer(GstBuffer *buffer,
                                                               GstMapInfo map,
                                                               uint32_t width,
                                                               uint32_t height,
                                                               pek::DataKind format,
                                                               uint32_t strideBytes,
                                                               pek::AccessMode accessMode) {
    if (buffer == nullptr || map.data == nullptr) {
        return nullptr;
    }

    if (strideBytes == 0) {
        strideBytes = defaultStride(width, format);
    }

    std::vector<DataView> planes;
    planes.push_back(DataView::host(map.data, map.size, format, strideBytes, accessMode, 0));

    return std::unique_ptr<GstVideoFrame>(new GstVideoFrame(buffer,
                                                            true,
                                                            map,
                                                            pek::MemoryType::Host,
                                                            format,
                                                            width,
                                                            height,
                                                            timestampNsFromGstBuffer(buffer),
                                                            std::move(planes),
                                                            accessMode));
}

std::unique_ptr<GstVideoFrame> GstVideoFrame::mapBuffer(GstBuffer *buffer,
                                                        uint32_t width,
                                                        uint32_t height,
                                                        pek::DataKind format,
                                                        uint32_t strideBytes,
                                                        pek::AccessMode accessMode) {
    if (buffer == nullptr || !validMapMode(accessMode)) {
        return nullptr;
    }

    GstMapInfo map{};
    if (!gst_buffer_map(buffer, &map, gstMapFlagsFromAccessMode(accessMode))) {
        return nullptr;
    }

    auto frame = takeMappedBuffer(buffer, map, width, height, format, strideBytes, accessMode);
    if (!frame) {
        gst_buffer_unmap(buffer, &map);
    }
    return frame;
}

std::unique_ptr<GstVideoFrame> GstVideoFrame::fromDmaBuf(GstBuffer *buffer,
                                                         int fd,
                                                         size_t byteSize,
                                                         uint32_t width,
                                                         uint32_t height,
                                                         uint32_t strideBytes,
                                                         pek::DataKind format,
                                                         size_t offsetBytes,
                                                         DmaBufSync sync) {
    if (fd < 0) {
        return nullptr;
    }

    std::vector<DataView> planes;
    planes.push_back(DataView::dmaBuf(
        fd, byteSize, format, strideBytes, offsetBytes, pek::AccessMode::ReadWrite, sync));

    return std::unique_ptr<GstVideoFrame>(new GstVideoFrame(buffer,
                                                            false,
                                                            GstMapInfo{},
                                                            pek::MemoryType::DmaBuf,
                                                            format,
                                                            width,
                                                            height,
                                                            timestampNsFromGstBuffer(buffer),
                                                            std::move(planes),
                                                            pek::AccessMode::Unknown));
}

pek::DataKind GstVideoFrame::format() const noexcept {
    return frameFormat;
}

uint32_t GstVideoFrame::width() const noexcept {
    return frameWidth;
}

uint32_t GstVideoFrame::height() const noexcept {
    return frameHeight;
}

TimestampNs GstVideoFrame::timestampNs() const noexcept {
    return frameTimestampNs;
}

pek::MemoryType GstVideoFrame::memoryType() const noexcept {
    return frameMemoryType;
}

std::span<const DataView> GstVideoFrame::planes() const noexcept {
    return framePlanes;
}

bool GstVideoFrame::canMap(pek::AccessMode mode) const noexcept {
    return buffer != nullptr && validMapMode(mode);
}

std::unique_ptr<pek::mediaio::VideoFrame> GstVideoFrame::map(pek::AccessMode mode) const {
    if (!canMap(mode)) {
        return nullptr;
    }

    return mapBuffer(buffer,
                     frameWidth,
                     frameHeight,
                     frameFormat,
                     framePlanes.empty() ? 0 : framePlanes.front().strideBytes(),
                     mode);
}

std::unique_ptr<pek::mediaio::VideoFrame>
makeVideoFrameFromMappedBuffer(GstBuffer *buffer,
                               GstMapInfo map,
                               uint32_t width,
                               uint32_t height,
                               pek::DataKind format,
                               uint32_t strideBytes,
                               pek::AccessMode accessMode) {
    return GstVideoFrame::takeMappedBuffer(
        buffer, map, width, height, format, strideBytes, accessMode);
}

std::unique_ptr<pek::mediaio::VideoFrame> mapVideoFrame(GstBuffer *buffer,
                                                        uint32_t width,
                                                        uint32_t height,
                                                        pek::DataKind format,
                                                        uint32_t strideBytes,
                                                        pek::AccessMode accessMode) {
    return GstVideoFrame::mapBuffer(buffer, width, height, format, strideBytes, accessMode);
}

std::unique_ptr<pek::mediaio::VideoFrame> makeDmaBufVideoFrame(GstBuffer *buffer,
                                                               int fd,
                                                               size_t byteSize,
                                                               uint32_t width,
                                                               uint32_t height,
                                                               uint32_t strideBytes,
                                                               pek::DataKind format,
                                                               size_t offsetBytes,
                                                               DmaBufSync sync) {
    return GstVideoFrame::fromDmaBuf(
        buffer, fd, byteSize, width, height, strideBytes, format, offsetBytes, sync);
}

} // namespace pek::mediaio::gst
