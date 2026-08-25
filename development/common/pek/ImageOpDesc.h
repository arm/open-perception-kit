/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "pek/Types.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace pek {

/// @brief Maximum number of image planes supported by preprocessing descriptors.
constexpr size_t MaxImagePlaneCount = 4;

/**
 * @brief Describes one image data plane.
 */
struct ImagePlaneDesc {
    const uint8_t *data = nullptr;  ///< Read-only pointer to the first byte of the plane.
    uint8_t *mutableData = nullptr; ///< Writable pointer to the first byte of the plane.
    size_t byteCount = 0;           ///< Number of bytes available from the plane pointer.
    size_t strideBytes = 0;         ///< Row stride in bytes.
};

/**
 * @brief Returns the writable plane pointer viewed as elements of type T.
 */
template <typename T> T *mutablePlaneData(const ImagePlaneDesc &plane) noexcept {
    static_assert(!std::is_const_v<T>, "mutablePlaneData requires a mutable element type");
    if constexpr (std::is_same_v<T, uint8_t>) {
        return plane.mutableData;
    } else {
        return static_cast<T *>(static_cast<void *>(plane.mutableData));
    }
}

/**
 * @brief Describes the buffers and preprocessing parameters used by image tensor conversion.
 */
struct ImageOpDesc {
    size_t surfaceWidth = 0;  ///< Surface width in pixels.
    size_t surfaceHeight = 0; ///< Surface height in pixels.

    PixelRect rect; ///< Region of interest within the surface.

    std::array<ImagePlaneDesc, MaxImagePlaneCount> planes{}; ///< Image data planes.
    size_t planeCount = 0;                                   ///< Number of valid entries in planes.

    RawImagePixelFormat format = RawImagePixelFormat::Unknown; ///< Raw source image pixel format.
    DataKind kind = DataKind::Unknown;     ///< Tensor/image semantic content and layout.
    pek::Dtype type = pek::Dtype::Float32; ///< Element data type of the buffer.

    YuvColorMatrix yuvMatrix = YuvColorMatrix::Unknown; ///< YUV-to-RGB matrix for YUV sources.
    YuvRange yuvRange = YuvRange::Unknown;              ///< Encoded numeric range for YUV sources.

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
