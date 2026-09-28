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
 * @file GstVideoFrame.cpp
 * @brief GStreamer-backed mediaio VideoFrame implementation.
 */

#include "mediaio/GstVideoFrame.h"

#include "Log.h"

#include <gst/allocators/gstdmabuf.h>

#include <atomic>
#include <cinttypes>
#include <cstdlib>
#include <limits>
#include <utility>

namespace opk::mediaio::gst {

namespace {

std::atomic_uint64_t gMapCount{0};
std::atomic_uint64_t gUnmapCount{0};

bool lifetimeDebugEnabled() noexcept {
    const char *value =
        std::getenv("OPK_GSTVIDEOFRAME_DEBUG_LIFETIME"); // NOLINT(concurrency-mt-unsafe)
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}

void maybePrintLifetimeCounters(const char *event, uint64_t eventCount) noexcept {
    if (!lifetimeDebugEnabled() || eventCount % 100 != 0) {
        return;
    }

    try {
        opk::log::error("[GstVideoFrame] {}={} maps={} unmaps={}\n",
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
 * @param mode OPK access mode to validate.
 * @return True when mode is Read, Write, or ReadWrite.
 */
bool validMapMode(opk::AccessMode mode) noexcept {
    return mode == opk::AccessMode::Read || mode == opk::AccessMode::Write ||
           mode == opk::AccessMode::ReadWrite;
}

/**
 * @brief Returns true when mode requests write access.
 */
bool needsWrite(opk::AccessMode mode) noexcept {
    return mode == opk::AccessMode::Write || mode == opk::AccessMode::ReadWrite;
}

/**
 * @brief Returns the OPK raw image pixel format represented by a GStreamer video format.
 * @param format GStreamer video format.
 * @return Matching OPK raw image pixel format, or Unknown when unsupported.
 */
opk::RawImagePixelFormat rawImagePixelFormatFromGstVideoFormat(GstVideoFormat format) noexcept {
    using enum opk::RawImagePixelFormat;

    switch (format) {
    case GST_VIDEO_FORMAT_BGRA:
        return Bgra;
    case GST_VIDEO_FORMAT_RGB:
        return Rgb;
    case GST_VIDEO_FORMAT_GRAY8:
        return Gray;
    case GST_VIDEO_FORMAT_I420:
        return I420;
    case GST_VIDEO_FORMAT_NV12:
        return Nv12;
    case GST_VIDEO_FORMAT_YUY2:
        return Yuy2;
    default:
        return Unknown;
    }
}

/**
 * @brief Returns true when @p info describes a video layout this wrapper can expose.
 */
bool supportedVideoInfo(const GstVideoInfo &info) noexcept {
    using enum opk::RawImagePixelFormat;

    return info.finfo != nullptr &&
           rawImagePixelFormatFromGstVideoFormat(GST_VIDEO_INFO_FORMAT(&info)) != Unknown &&
           GST_VIDEO_INFO_WIDTH(&info) > 0 && GST_VIDEO_INFO_HEIGHT(&info) > 0 &&
           GST_VIDEO_INFO_N_PLANES(&info) > 0;
}

bool isYuvFormat(opk::RawImagePixelFormat format) noexcept {
    using enum opk::RawImagePixelFormat;

    switch (format) {
    case I420:
    case Nv12:
    case Yuy2:
        return true;
    default:
        return false;
    }
}

opk::YuvColorMatrix defaultYuvColorMatrix(uint32_t height) noexcept {
    using enum opk::YuvColorMatrix;

    return height <= 576 ? Bt601 : Bt709;
}

opk::YuvColorMatrix yuvColorMatrixFromGst(GstVideoColorimetry colorimetry,
                                          uint32_t height) noexcept {
    using enum opk::YuvColorMatrix;

    switch (colorimetry.matrix) {
    case GST_VIDEO_COLOR_MATRIX_BT601:
        return Bt601;
    case GST_VIDEO_COLOR_MATRIX_BT709:
        return Bt709;
    case GST_VIDEO_COLOR_MATRIX_BT2020:
        return Bt2020;
    case GST_VIDEO_COLOR_MATRIX_UNKNOWN:
        return defaultYuvColorMatrix(height);
    default:
        return Unknown;
    }
}

opk::YuvRange yuvRangeFromGst(GstVideoColorimetry colorimetry) noexcept {
    using enum opk::YuvRange;

    switch (colorimetry.range) {
    case GST_VIDEO_COLOR_RANGE_0_255:
        return Full;
    case GST_VIDEO_COLOR_RANGE_16_235:
        return Limited;
    case GST_VIDEO_COLOR_RANGE_UNKNOWN:
        return Limited;
    default:
        return Unknown;
    }
}

opk::YuvColorMatrix yuvColorMatrixFromGstVideoInfo(const GstVideoInfo &info) noexcept {
    using enum opk::YuvColorMatrix;

    if (const auto format = rawImagePixelFormatFromGstVideoFormat(GST_VIDEO_INFO_FORMAT(&info));
        !isYuvFormat(format)) {
        return Unknown;
    }
    return yuvColorMatrixFromGst(GST_VIDEO_INFO_COLORIMETRY(&info),
                                 static_cast<uint32_t>(GST_VIDEO_INFO_HEIGHT(&info)));
}

opk::YuvRange yuvRangeFromGstVideoInfo(const GstVideoInfo &info) noexcept {
    using enum opk::YuvRange;

    if (const auto format = rawImagePixelFormatFromGstVideoFormat(GST_VIDEO_INFO_FORMAT(&info));
        !isYuvFormat(format)) {
        return Unknown;
    }
    return yuvRangeFromGst(GST_VIDEO_INFO_COLORIMETRY(&info));
}

/**
 * @brief Returns the byte range needed to expose a mapped video plane.
 */
size_t
mappedPlaneByteSize(const GstVideoInfo &info, const ::GstVideoFrame &frame, guint plane) noexcept {
    const gint stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, plane);
    const guint height =
        GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT(info.finfo, plane, GST_VIDEO_INFO_HEIGHT(&info));
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

opk::AccessMode accessModeFromGstMapFlags(GstMapFlags flags) noexcept {
    const bool read = (flags & GST_MAP_READ) != 0;
    const bool write = (flags & GST_MAP_WRITE) != 0;

    if (read && write) {
        return opk::AccessMode::ReadWrite;
    }
    if (write) {
        return opk::AccessMode::Write;
    }
    if (read) {
        return opk::AccessMode::Read;
    }
    return opk::AccessMode::Unknown;
}

GstMapFlags gstMapFlagsFromAccessMode(opk::AccessMode mode) noexcept {
    switch (mode) {
    case opk::AccessMode::Read:
        return GST_MAP_READ;
    case opk::AccessMode::Write:
        return GST_MAP_WRITE;
    case opk::AccessMode::ReadWrite:
        return static_cast<GstMapFlags>(GST_MAP_READ | GST_MAP_WRITE);
    case opk::AccessMode::Unknown:
        break;
    }
    return GST_MAP_READ;
}

GstVideoFrame::GstVideoFrame(GstBuffer *buffer,
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
                             opk::AccessMode mappedAccessMode) noexcept
    : buffer(buffer != nullptr ? gst_buffer_ref(buffer) : nullptr), videoFrameMap(frameMap),
      frameVideoInfo(videoInfo), frameMemoryType(memoryType), frameFormat(format),
      frameYuvMatrix(yuvMatrix), frameYuvRange(yuvRange), frameWidth(width), frameHeight(height),
      frameTimestampNs(timestampNs), framePlanes(std::move(planes)),
      mappedAccess(mappedAccessMode) {}

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
                                                                 opk::AccessMode accessMode) {
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

    const auto format = rawImagePixelFormatFromGstVideoFormat(GST_VIDEO_INFO_FORMAT(&videoInfo));
    const auto planeCount = GST_VIDEO_INFO_N_PLANES(&videoInfo);
    std::vector<DataView> planes;
    planes.reserve(planeCount);
    for (guint plane = 0; plane < planeCount; ++plane) {
        auto *data = GST_VIDEO_FRAME_PLANE_DATA(&frameMap, plane);
        const gint stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frameMap, plane);
        const size_t byteSize = mappedPlaneByteSize(videoInfo, frameMap, plane);
        if (data == nullptr || stride <= 0 || byteSize == 0) {
            gst_video_frame_unmap(&frameMap);
            return nullptr;
        }

        planes.push_back(
            DataView::host(data, byteSize, static_cast<uint32_t>(stride), accessMode, 0));
    }

    if (planes.empty()) {
        gst_video_frame_unmap(&frameMap);
        return nullptr;
    }

    const auto mapCount = gMapCount.fetch_add(1, std::memory_order_relaxed) + 1;
    maybePrintLifetimeCounters("maps", mapCount);

    return std::unique_ptr<GstVideoFrame>( // NOSONAR - constructor is private; make_unique cannot
                                           // access it.
        new GstVideoFrame(buffer,
                          frameMap,
                          videoInfo,
                          opk::MemoryType::Host,
                          format,
                          yuvColorMatrixFromGstVideoInfo(videoInfo),
                          yuvRangeFromGstVideoInfo(videoInfo),
                          static_cast<uint32_t>(GST_VIDEO_INFO_WIDTH(&videoInfo)),
                          static_cast<uint32_t>(GST_VIDEO_INFO_HEIGHT(&videoInfo)),
                          timestampNsFromGstBuffer(buffer),
                          std::move(planes),
                          accessMode));
}

std::shared_ptr<GstVideoFrame> GstVideoFrame::mapGstBuffer(GstBuffer *buffer,
                                                           const GstVideoInfo &videoInfo,
                                                           opk::AccessMode accessMode) {
    return std::shared_ptr<GstVideoFrame>(mapGstBufferUnique(buffer, videoInfo, accessMode));
}

opk::RawImagePixelFormat GstVideoFrame::format() const noexcept {
    return frameFormat;
}

opk::YuvColorMatrix GstVideoFrame::yuvColorMatrix() const noexcept {
    return frameYuvMatrix;
}

opk::YuvRange GstVideoFrame::yuvRange() const noexcept {
    return frameYuvRange;
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

opk::MemoryType GstVideoFrame::memoryType() const noexcept {
    return frameMemoryType;
}

std::span<const DataView> GstVideoFrame::planes() const noexcept {
    return framePlanes;
}

bool GstVideoFrame::canMap(opk::AccessMode mode) const noexcept {
    if (buffer == nullptr || !validMapMode(mode) || !supportedVideoInfo(frameVideoInfo)) {
        return false;
    }
    if (needsWrite(mode) && !gst_buffer_is_writable(buffer)) {
        return false;
    }
    return true;
}

std::unique_ptr<opk::mediaio::VideoFrame> GstVideoFrame::map(opk::AccessMode mode) const {
    if (!canMap(mode)) {
        return nullptr;
    }

    return mapGstBufferUnique(buffer, frameVideoInfo, mode);
}

} // namespace opk::mediaio::gst
