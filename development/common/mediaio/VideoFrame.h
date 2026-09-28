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
 * @file VideoFrame.h
 * @brief Backend-neutral video frame interface used between media IO and inference code.
 */

#pragma once

#include "mediaio/Common.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace opk::mediaio {

/**
 * @brief Abstract video frame boundary between media backends and inference code.
 *
 * Implementations own or borrow backend-specific resources and expose only OPK
 * data views. CPU mapping is explicit and returns another VideoFrame whose
 * lifetime owns the map until that mapped frame is destroyed.
 */
class VideoFrame {
  public:
    /// Destroys the frame and releases any backend-owned mapping or reference.
    virtual ~VideoFrame() = default;

    /** @brief Returns the frame pixel layout. */
    virtual opk::RawImagePixelFormat format() const noexcept = 0;

    /** @brief Returns the YUV-to-RGB matrix for YUV frames, or Unknown when not applicable. */
    virtual opk::YuvColorMatrix yuvColorMatrix() const noexcept {
        return opk::YuvColorMatrix::Unknown;
    }

    /** @brief Returns the encoded YUV sample range, or Unknown when not applicable. */
    virtual opk::YuvRange yuvRange() const noexcept {
        return opk::YuvRange::Unknown;
    }

    /** @brief Returns the frame width in pixels. */
    virtual uint32_t width() const noexcept = 0;

    /** @brief Returns the frame height in pixels. */
    virtual uint32_t height() const noexcept = 0;

    /** @brief Returns the frame presentation timestamp in nanoseconds. */
    virtual TimestampNs timestampNs() const noexcept = 0;

    /** @brief Returns the backing memory type used by the exposed planes. */
    virtual opk::MemoryType memoryType() const noexcept = 0;

    /**
     * @brief Returns the frame's data planes.
     * @return Non-owning plane views valid for this VideoFrame lifetime.
     */
    virtual std::span<const DataView> planes() const noexcept = 0;

    /**
     * @brief Returns true when map() can provide the requested access mode.
     * @param mode Requested CPU access mode.
     */
    virtual bool canMap(opk::AccessMode mode) const noexcept = 0;

    /**
     * @brief Maps or aliases the frame as CPU-addressable memory.
     * @param mode Requested CPU access mode.
     * @return Mapped frame, or nullptr if the requested mapping cannot be provided.
     */
    virtual std::unique_ptr<VideoFrame> map(opk::AccessMode mode) const = 0;

    /** @brief Returns true when the frame has no usable dimensions or planes. */
    bool empty() const noexcept;

    /** @brief Returns the number of exposed data planes. */
    size_t planeCount() const noexcept;

    /**
     * @brief Returns a plane by index.
     * @param index Zero-based plane index.
     * @return Plane view, or nullptr when index is out of range.
     */
    const DataView *plane(size_t index) const noexcept;
};

} // namespace opk::mediaio
