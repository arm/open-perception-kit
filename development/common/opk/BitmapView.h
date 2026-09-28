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

#pragma once

#include "opk/Types.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace opk {

/**
 * @brief Non-owning view over bitmap/image memory.
 *
 * Lifetime note: data is not owned and must outlive this view.
 */
struct BitmapView {

    /** @brief Constructs an empty bitmap view. */
    BitmapView() = default;

    /**
     * @brief Constructs a view over external pixel memory.
     * @param data Pointer to first byte of image data.
     * @param dataKind Pixel/data layout kind.
     * @param width Image width in pixels.
     * @param height Image height in pixels.
     * @param stride Bytes per row. Use 0 for tightly packed rows.
     */
    BitmapView(
        uint8_t *data, opk::DataKind dataKind, size_t width, size_t height, size_t stride = 0) {
        this->data = data;
        this->dataKind = dataKind;
        this->width = width;
        this->height = height;
        this->stride = stride;
    }

    /**
     * @brief Returns true when stride is implied (tightly packed rows).
     */
    bool hasImplicitStride() const {
        return stride == 0;
    }

    /**
     * @brief Returns bytes per pixel for known image data kinds.
     * @return 1 (gray), 3 (RGB), 4 (BGRA), or 0 for unsupported/unknown kinds.
     */
    size_t getBytesPerPixel() const {
        switch (dataKind) {
        case opk::DataKind::ImageGray:
            return 1;
        case opk::DataKind::ImageRgbHwc:
        case opk::DataKind::ImageRgbChw:
            return 3;
        case opk::DataKind::ImageBgraHwc:
            return 4;
        default:
            return 0;
        }
    }

    /**
     * @brief Returns effective row stride in bytes.
     *
     * If explicit stride is 0, this returns width * bytesPerPixel for known image kinds.
     */
    size_t getEffectiveStride() const {
        if (stride != 0)
            return stride;
        const size_t bytesPerPixel = getBytesPerPixel();
        if (bytesPerPixel == 0)
            return 0;
        return width * bytesPerPixel;
    }

    /// Pointer to external pixel memory (non-owning).
    uint8_t *data = nullptr;
    /// Image width in pixels.
    size_t width = 0;
    /// Image height in pixels.
    size_t height = 0;
    /// Row stride in bytes. 0 means tightly packed rows.
    size_t stride = 0;
    /// Pixel/data layout kind for this view.
    opk::DataKind dataKind = opk::DataKind::Unknown;
};

} // namespace opk
