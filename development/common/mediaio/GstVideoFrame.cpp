/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

/**
 * @file GstVideoFrame.cpp
 * @brief GStreamer-backed mediaio VideoFrame implementation.
 */

#include "mediaio/GstVideoFrame.h"

#include "pek/Log.h"

#include <gst/allocators/gstdmabuf.h>

#include <atomic>
#include <cinttypes>
#include <cstdlib>
#include <limits>
#include <utility>

namespace pek::mediaio::gst {

namespace {

std::atomic_uint64_t gMapCount{0};
std::atomic_uint64_t gUnmapCount{0};

bool lifetimeDebugEnabled() noexcept {
    const char *value =
        std::getenv("PEK_GSTVIDEOFRAME_DEBUG_LIFETIME"); // NOLINT(concurrency-mt-unsafe)
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}

void maybePrintLifetimeCounters(const char *event, uint64_t eventCount) noexcept {
    if (!lifetimeDebugEnabled() || eventCount % 100 != 0) {
        return;
    }

    try {
        pek::loge("[GstVideoFrame] {}={} maps={} unmaps={}\n",
                  event,
                  eventCount,
                  gMapCount.load(std::memory_order_relaxed),
                  gUnmapCount.load(std::memory_order_relaxed));
    } catch (...) {
        // Lifetime diagnostics are best-effort and must not affect frame mapping or cleanup.
        return;
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
 * @brief Returns true when mode requests write access.
 */
bool needsWrite(pek::AccessMode mode) noexcept {
    return mode == pek::AccessMode::Write || mode == pek::AccessMode::ReadWrite;
}

/**
 * @brief Returns the PEK data kind represented by a GStreamer video format.
 * @param format GStreamer video format.
 * @return Matching PEK image kind, or Unknown when unsupported.
 */
pek::DataKind dataKindFromGstVideoFormat(GstVideoFormat format) noexcept {
    switch (format) {
    case GST_VIDEO_FORMAT_BGRA:
        return pek::DataKind::ImageBgraHwc;
    case GST_VIDEO_FORMAT_RGB:
        return pek::DataKind::ImageRgbHwc;
    case GST_VIDEO_FORMAT_GRAY8:
        return pek::DataKind::ImageGray;
    default:
        return pek::DataKind::Unknown;
    }
}

/**
 * @brief Returns true when @p info describes a video layout this wrapper can expose.
 */
bool supportedVideoInfo(const GstVideoInfo &info) noexcept {
    return info.finfo != nullptr &&
           dataKindFromGstVideoFormat(GST_VIDEO_INFO_FORMAT(&info)) != pek::DataKind::Unknown &&
           GST_VIDEO_INFO_WIDTH(&info) > 0 && GST_VIDEO_INFO_HEIGHT(&info) > 0 &&
           GST_VIDEO_INFO_N_PLANES(&info) > 0;
}

/**
 * @brief Returns the byte range needed to expose the first mapped video plane.
 */
size_t mappedFirstPlaneByteSize(const GstVideoInfo &info, const ::GstVideoFrame &frame) noexcept {
    const gint stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0);
    const guint height = GST_VIDEO_INFO_HEIGHT(&info);
    if (stride <= 0 || height == 0) {
        return 0;
    }

    const auto strideBytes = static_cast<size_t>(stride);
    if (strideBytes > std::numeric_limits<size_t>::max() / static_cast<size_t>(height)) {
        return 0;
    }

    return strideBytes * static_cast<size_t>(height);
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

/**
 * @brief Returns true when any memory block in @p buffer is DMA-BUF-backed.
 */
bool bufferHasDmaBufContent(GstBuffer *buffer) noexcept {
    if (buffer == nullptr) {
        return false;
    }

    const guint memoryCount = gst_buffer_n_memory(buffer);
    for (guint i = 0; i < memoryCount; ++i) {
        auto *memory = gst_buffer_peek_memory(buffer, i);
        if (memory != nullptr && gst_is_dmabuf_memory(memory)) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Returns true when every memory block in @p buffer is system memory.
 */
bool bufferHasDirectCpuAddress(GstBuffer *buffer) noexcept {
    if (buffer == nullptr) {
        return false;
    }

    const guint memoryCount = gst_buffer_n_memory(buffer);
    if (memoryCount == 0) {
        return false;
    }

    for (guint i = 0; i < memoryCount; ++i) {
        auto *memory = gst_buffer_peek_memory(buffer, i);
        if (memory == nullptr || !gst_memory_is_type(memory, GST_ALLOCATOR_SYSMEM)) {
            return false;
        }
    }

    return true;
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
                             ::GstVideoFrame frameMap,
                             GstVideoInfo videoInfo,
                             pek::MemoryType memoryType,
                             pek::DataKind format,
                             uint32_t width,
                             uint32_t height,
                             TimestampNs timestampNs,
                             std::vector<DataView> planes,
                             pek::AccessMode mappedAccessMode) noexcept
    : buffer(buffer != nullptr ? gst_buffer_ref(buffer) : nullptr), videoFrameMap(frameMap),
      frameVideoInfo(videoInfo), frameMemoryType(memoryType), frameFormat(format),
      frameWidth(width), frameHeight(height), frameTimestampNs(timestampNs),
      framePlanes(std::move(planes)), mappedAccess(mappedAccessMode) {}

GstVideoFrame::~GstVideoFrame() {
    gst_video_frame_unmap(&videoFrameMap);
    const auto unmapCount = gUnmapCount.fetch_add(1, std::memory_order_relaxed) + 1;
    maybePrintLifetimeCounters("unmaps", unmapCount);
    if (buffer != nullptr) {
        gst_buffer_unref(buffer);
    }
}

bool GstVideoFrame::hasDmaBufContent(GstBuffer *buffer) noexcept {
    return bufferHasDmaBufContent(buffer);
}

bool GstVideoFrame::hasDirectCpuAddress(GstBuffer *buffer) noexcept {
    return bufferHasDirectCpuAddress(buffer);
}

std::unique_ptr<GstVideoFrame> GstVideoFrame::mapGstBufferUnique(GstBuffer *buffer,
                                                                 const GstVideoInfo &videoInfo,
                                                                 pek::AccessMode accessMode) {
    if (buffer == nullptr || !validMapMode(accessMode) || !supportedVideoInfo(videoInfo)) {
        return nullptr;
    }
    if (needsWrite(accessMode) && !gst_buffer_is_writable(buffer)) {
        return nullptr;
    }

    const auto mapFlags =
        static_cast<GstMapFlags>(gstMapFlagsFromAccessMode(accessMode) |
                                 static_cast<GstMapFlags>(GST_VIDEO_FRAME_MAP_FLAG_NO_REF));

    ::GstVideoFrame frameMap{};
    if (!gst_video_frame_map(&frameMap, &videoInfo, buffer, mapFlags)) {
        return nullptr;
    }

    auto *data = GST_VIDEO_FRAME_PLANE_DATA(&frameMap, 0);
    const gint stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frameMap, 0);
    const size_t byteSize = mappedFirstPlaneByteSize(videoInfo, frameMap);
    if (data == nullptr || stride <= 0 || byteSize == 0) {
        gst_video_frame_unmap(&frameMap);
        return nullptr;
    }

    const auto format = dataKindFromGstVideoFormat(GST_VIDEO_INFO_FORMAT(&videoInfo));
    const auto mapCount = gMapCount.fetch_add(1, std::memory_order_relaxed) + 1;
    maybePrintLifetimeCounters("maps", mapCount);

    std::vector<DataView> planes;
    planes.push_back(
        DataView::host(data, byteSize, format, static_cast<uint32_t>(stride), accessMode, 0));

    return std::unique_ptr<GstVideoFrame>(
        new GstVideoFrame(buffer,
                          frameMap,
                          videoInfo,
                          pek::MemoryType::Host,
                          format,
                          static_cast<uint32_t>(GST_VIDEO_INFO_WIDTH(&videoInfo)),
                          static_cast<uint32_t>(GST_VIDEO_INFO_HEIGHT(&videoInfo)),
                          timestampNsFromGstBuffer(buffer),
                          std::move(planes),
                          accessMode));
}

std::shared_ptr<GstVideoFrame> GstVideoFrame::mapGstBuffer(GstBuffer *buffer,
                                                           const GstVideoInfo &videoInfo,
                                                           pek::AccessMode accessMode) {
    return std::shared_ptr<GstVideoFrame>(mapGstBufferUnique(buffer, videoInfo, accessMode));
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
    if (buffer == nullptr || !validMapMode(mode) || !supportedVideoInfo(frameVideoInfo)) {
        return false;
    }
    if (needsWrite(mode) && !gst_buffer_is_writable(buffer)) {
        return false;
    }
    return true;
}

std::unique_ptr<pek::mediaio::VideoFrame> GstVideoFrame::map(pek::AccessMode mode) const {
    if (!canMap(mode)) {
        return nullptr;
    }

    return mapGstBufferUnique(buffer, frameVideoInfo, mode);
}

} // namespace pek::mediaio::gst
