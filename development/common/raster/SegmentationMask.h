/*
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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
 * @file SegmentationMask.h
 * @brief Full-surface raster operations driven by segmentation masks.
 */

#pragma once

#include "opk/Bitmap.h"
#include "opk/Color.h"
#include "opk/ImageOpDesc.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace opk::raster {

/**
 * @brief Writable image surface used by full-frame raster operations.
 */
struct ImageSurfaceView {
    /** @brief Pixel layout of the target image. */
    opk::RawImagePixelFormat format = opk::RawImagePixelFormat::Unknown;

    /** @brief Target width in pixels. */
    std::uint32_t width = 0;

    /** @brief Target height in pixels. */
    std::uint32_t height = 0;

    /** @brief Writable target image planes. */
    std::span<opk::ImagePlaneDesc> planes{};

    /** @brief YUV color matrix for YUV targets. */
    opk::YuvColorMatrix yuvMatrix = opk::YuvColorMatrix::Unknown;

    /** @brief YUV numeric range for YUV targets. */
    opk::YuvRange yuvRange = opk::YuvRange::Unknown;
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
    opk::Color color = opk::colorFromRgbBytes(100, 255, 255);

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
    opk::Color fallbackColor = opk::colorFromRgbBytes(0, 255, 255);

    /** @brief Optional BGRA background image. */
    const opk::Bitmap *backgroundImage = nullptr;
};

/**
 * @brief Alpha-blends a segmentation mask over @p surface.
 * @return True when input validation passed and rendering was attempted.
 */
[[nodiscard]] bool
blendSegmentationMask(const ImageSurfaceView &surface,
                      const MaskView &mask,
                      const SegmentationMaskOptions &options = SegmentationMaskOptions{}) noexcept;

/**
 * @brief Replaces background pixels selected by a binary segmentation mask.
 * @return True when input validation passed and rendering was attempted.
 */
[[nodiscard]] bool replaceBackgroundFromMask(
    const ImageSurfaceView &surface,
    const MaskView &mask,
    const BackgroundReplacementOptions &options = BackgroundReplacementOptions{}) noexcept;

} // namespace opk::raster
