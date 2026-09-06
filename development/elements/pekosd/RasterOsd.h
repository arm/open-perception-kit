/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

/**
 * @file RasterOsd.h
 * @brief Raster-backed rendering boundary for the PEK OSD element.
 */

#pragma once

#include "pek/Bitmap.h"
#include "pek/FrameResults.h"
#include "pek/ImageOpDesc.h"

#include <cstdint>
#include <span>

namespace pek::osd {

/**
 * @brief Writable video surface used by the raster OSD renderer.
 */
struct RasterSurface {
    /** @brief Pixel layout of the target video frame. */
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
 * @brief Runtime options that affect which overlays are drawn.
 */
struct RasterDrawOptions {
    /** @brief Draw PerformanceOverlayT payloads when present. */
    bool performanceOverlayEnabled = true;

    /** @brief Optional BGRA replacement image used by segmentation background replacement. */
    const pek::Bitmap *backgroundImage = nullptr;
};

/**
 * @brief Complete raster OSD draw request.
 */
struct RasterDrawRequest {
    /** @brief Destination surface to draw into. */
    RasterSurface surface{};

    /** @brief FrameResults metadata that drives overlay rendering. */
    const perception::FrameResults *frameResults = nullptr;

    /** @brief OSD rendering options. */
    RasterDrawOptions options{};
};

/**
 * @brief Result of dispatching a raster OSD draw request.
 */
enum class RasterDrawStatus {
    /** Request was accepted and drawing completed. */
    Drawn,

    /** No FrameResults metadata was supplied. */
    MissingFrameResults,

    /** Target pixel format is not supported by the raster OSD path. */
    UnsupportedFormat,

    /** Target planes, dimensions, or strides are not writable/valid. */
    InvalidSurface
};

/**
 * @brief Returns true when the raster OSD path can target @p format.
 */
[[nodiscard]] bool supportsRasterSurfaceFormat(pek::RawImagePixelFormat format) noexcept;

/**
 * @brief Validates and dispatches raster OSD rendering for a frame.
 *
 * This entry point is the handoff boundary from the GStreamer element into the
 * direct raster implementation.
 */
[[nodiscard]] RasterDrawStatus drawRasterOsd(const RasterDrawRequest &request) noexcept;

} // namespace pek::osd
