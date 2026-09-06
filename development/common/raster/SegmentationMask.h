/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

/**
 * @file SegmentationMask.h
 * @brief Full-surface raster operations driven by segmentation masks.
 */

#pragma once

#include "pek/Bitmap.h"
#include "pek/Color.h"
#include "pek/ImageOpDesc.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace pek::raster {

/**
 * @brief Writable image surface used by full-frame raster operations.
 */
struct ImageSurfaceView {
    /** @brief Pixel layout of the target image. */
    pek::RawImagePixelFormat format = pek::RawImagePixelFormat::Unknown;

    /** @brief Target width in pixels. */
    std::uint32_t width = 0;

    /** @brief Target height in pixels. */
    std::uint32_t height = 0;

    /** @brief Writable target image planes. */
    std::span<pek::ImagePlaneDesc> planes{};

    /** @brief YUV color matrix for YUV targets. */
    pek::YuvColorMatrix yuvMatrix = pek::YuvColorMatrix::Unknown;

    /** @brief YUV numeric range for YUV targets. */
    pek::YuvRange yuvRange = pek::YuvRange::Unknown;
};

/**
 * @brief Read-only 8-bit segmentation mask.
 */
struct MaskView {
    /** @brief Pointer to tightly packed mask pixels. */
    const std::uint8_t *data = nullptr;

    /** @brief Number of bytes available at @ref data. */
    std::size_t size = 0;

    /** @brief Mask width in pixels. */
    std::size_t width = 0;

    /** @brief Mask height in pixels. */
    std::size_t height = 0;
};

/**
 * @brief Options for alpha-blended segmentation mask visualization.
 */
struct SegmentationMaskOptions {
    /** @brief Overlay color blended over the existing image. */
    pek::Color color = pek::colorFromRgbBytes(100, 255, 255);

    /** @brief Upper alpha bound applied to mask values. */
    std::uint8_t maxAlpha = 255;
};

/**
 * @brief Options for binary background replacement from a segmentation mask.
 */
struct BackgroundReplacementOptions {
    /** @brief Pixels with mask values at or above this value are replaced. */
    std::uint8_t threshold = 150;

    /** @brief Solid replacement color used when no background image is supplied. */
    pek::Color fallbackColor = pek::colorFromRgbBytes(0, 255, 255);

    /** @brief Optional BGRA background image. */
    const pek::Bitmap *backgroundImage = nullptr;
};

/**
 * @brief Alpha-blends a segmentation mask over @p surface.
 * @return True when input validation passed and rendering was attempted.
 */
[[nodiscard]] bool blendSegmentationMask(ImageSurfaceView surface,
                                         MaskView mask,
                                         SegmentationMaskOptions options = {}) noexcept;

/**
 * @brief Replaces background pixels selected by a binary segmentation mask.
 * @return True when input validation passed and rendering was attempted.
 */
[[nodiscard]] bool replaceBackgroundFromMask(ImageSurfaceView surface,
                                             MaskView mask,
                                             BackgroundReplacementOptions options = {}) noexcept;

} // namespace pek::raster
