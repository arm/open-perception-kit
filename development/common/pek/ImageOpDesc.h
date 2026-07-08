/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Types.h"

#include <algorithm>
#include <cmath>

namespace pek {

/**
 * @brief Describes the layout and normalisation parameters of an image tensor buffer.
 */
struct ImageOpDesc {
    uint8_t *data = nullptr; ///< Pointer to the image data buffer.
    size_t byteCount = 0;    ///< Total size of the buffer in bytes.

    size_t surfaceWidth = 0;  ///< Surface width in pixels.
    size_t surfaceHeight = 0; ///< Surface height in pixels.
    size_t surfaceStride = 0; ///< Row stride in bytes; reserved, currently assumed tightly packed.

    PixelRect rect; ///< Region of interest within the surface.

    DataKind kind = DataKind::Unknown;     ///< Semantic content of the buffer.
    pek::Dtype type = pek::Dtype::Float32; ///< Element data type of the buffer.

    pek::Colorf mean = {0.0f, 0.0f, 0.0f, 0.0f}; ///< Per-channel normalisation mean.
    pek::Colorf std = {1.0f, 1.0f, 1.0f, 1.0f};  ///< Per-channel normalisation std.

    bool keepAspectRatio = false; ///< Preserve aspect ratio during resize by letterboxing.

    float letterboxRed = 114.0f / 255.0f;   ///< Letterbox red channel, normalized.
    float letterboxGreen = 114.0f / 255.0f; ///< Letterbox green channel, normalized.
    float letterboxBlue = 114.0f / 255.0f;  ///< Letterbox blue channel, normalized.

    /**
     * @brief Returns the number of channels implied by the DataKind, or 0 if unknown.
     */
    size_t getChannelCount() const {
        switch (kind) {
        case DataKind::ImageRgbChw:
            return 3;
        case DataKind::ImageRgbHwc:
            return 3;
        case DataKind::ImageBgraHwc:
            return 4;
        case DataKind::ImageGray:
            return 1;
        default:
            return 0;
        }
    }

    /**
     * @brief Returns true if the region of interest covers the full surface.
     */
    bool rectIsFullSurface() const {
        return rect.x == 0 && rect.y == 0 && rect.width == surfaceWidth &&
               rect.height == surfaceHeight;
    }
};

inline PixelRect computeLetterboxInnerRect(const PixelRect &sourceRect,
                                           const PixelRect &destinationRect) {
    if (sourceRect.isEmpty() || destinationRect.isEmpty()) {
        return {};
    }

    const double scaleX =
        static_cast<double>(destinationRect.width) / static_cast<double>(sourceRect.width);
    const double scaleY =
        static_cast<double>(destinationRect.height) / static_cast<double>(sourceRect.height);
    const double scale = std::min(scaleX, scaleY);

    auto innerWidth =
        static_cast<size_t>(std::llround(static_cast<double>(sourceRect.width) * scale));
    auto innerHeight =
        static_cast<size_t>(std::llround(static_cast<double>(sourceRect.height) * scale));

    innerWidth = std::clamp(innerWidth, size_t{1}, destinationRect.width);
    innerHeight = std::clamp(innerHeight, size_t{1}, destinationRect.height);

    return PixelRect{
        destinationRect.x + (destinationRect.width - innerWidth) / 2,
        destinationRect.y + (destinationRect.height - innerHeight) / 2,
        innerWidth,
        innerHeight,
    };
}

} // namespace pek
