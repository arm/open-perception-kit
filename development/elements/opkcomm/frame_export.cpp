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

#include "frame_export.h"

#include <cstring>
#include <limits>
#include <mediaio/GstVideoFrame.h>
#include <stdexcept>

OpkCommFrame captureFrame(GstBuffer *buffer, const GstVideoInfo &info) {
    if (GST_VIDEO_INFO_FORMAT(&info) != GST_VIDEO_FORMAT_BGRA || GST_VIDEO_INFO_WIDTH(&info) <= 0 ||
        GST_VIDEO_INFO_HEIGHT(&info) <= 0) {
        throw std::runtime_error("frame export requires negotiated BGRA video");
    }
    const auto width = static_cast<uint32_t>(GST_VIDEO_INFO_WIDTH(&info));
    const auto height = static_cast<uint32_t>(GST_VIDEO_INFO_HEIGHT(&info));
    if (width > std::numeric_limits<size_t>::max() / 4 / height) {
        throw std::runtime_error("BGRA frame size overflows size_t");
    }
    using opk::mediaio::gst::GstVideoFrame;
    if (!GstVideoFrame::hasDirectCpuAddress(buffer)) {
        throw std::runtime_error("frame export requires CPU system memory");
    }
    auto mapped = GstVideoFrame::mapGstBuffer(buffer, info, opk::AccessMode::Read);
    if (!mapped) {
        throw std::runtime_error("could not map frame for export");
    }
    const auto *plane = mapped->plane(0);
    const size_t rowBytes = size_t(width) * 4;
    if (!plane || !plane->data() || plane->strideBytes() < rowBytes ||
        size_t(height - 1) * plane->strideBytes() + rowBytes > plane->byteSize()) {
        throw std::runtime_error("invalid BGRA frame layout for export");
    }
    OpkCommFrame frame{width, height, std::vector<uint8_t>(rowBytes * height)};
    const auto *source = static_cast<const uint8_t *>(plane->data());
    for (size_t row = 0; row < height; ++row) {
        std::memcpy(
            frame.pixels.data() + row * rowBytes, source + row * plane->strideBytes(), rowBytes);
    }
    return frame;
}
