/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Types.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace pek {

/**
 * @brief Non-owning view over bitmap/image memory.
 *
 * Lifetime note: data is not owned and must outlive this view.
 */
struct BitmapView {

    /** @brief Constructs an empty bitmap view. */
    BitmapView() {}

    /**
     * @brief Constructs a view over external pixel memory.
     * @param data Pointer to first byte of image data.
     * @param dataKind Pixel/data layout kind.
     * @param width Image width in pixels.
     * @param height Image height in pixels.
     * @param stride Bytes per row. Use 0 for tightly packed rows.
     */
    BitmapView(
        uint8_t *data, pek::DataKind dataKind, size_t width, size_t height, size_t stride = 0) {
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
        case pek::DataKind::ImageGray:
            return 1;
        case pek::DataKind::ImageRgbHwc:
        case pek::DataKind::ImageRgbChw:
            return 3;
        case pek::DataKind::ImageBgraHwc:
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
    pek::DataKind dataKind = pek::DataKind::Unknown;
};

} // namespace pek
