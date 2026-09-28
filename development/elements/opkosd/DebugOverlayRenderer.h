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
 * @file DebugOverlayRenderer.h
 * @brief Debug decoration rendering boundary for the OPK OSD element.
 */

#pragma once

#include "opk/Bitmap.h"
#include "opk/FrameResults.h"
#include "opk/ImageOpDesc.h"

#include <cstdint>
#include <span>

namespace opk::osd {

/**
 * @brief Writable video surface used by the debug overlay renderer.
 */
struct DebugOverlaySurface {
    /** @brief Pixel layout of the target video frame. */
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
 * @brief Runtime options that affect which debug decorations are drawn.
 */
struct DebugOverlayOptions {
    /** @brief Draw PerformanceOverlayT payloads when present. */
    bool performanceOverlayEnabled = true;

    /** @brief Optional BGRA replacement image used by segmentation background replacement. */
    const opk::Bitmap *backgroundImage = nullptr;
};

/**
 * @brief Complete debug overlay draw request.
 */
struct DebugOverlayRequest {
    /** @brief Destination surface to draw into. */
    DebugOverlaySurface surface{};

    /** @brief FrameResults metadata that drives overlay rendering. */
    const open_perception_kit::FrameResults *frameResults = nullptr;

    /** @brief Debug overlay rendering options. */
    DebugOverlayOptions options{};
};

/**
 * @brief Result of dispatching a debug overlay draw request.
 */
enum class DebugOverlayStatus {
    /** Request was accepted and drawing completed. */
    Drawn,

    /** No FrameResults metadata was supplied. */
    MissingFrameResults,

    /** Target pixel format is not supported by the debug overlay renderer. */
    UnsupportedFormat,

    /** Target planes, dimensions, or strides are not writable/valid. */
    InvalidSurface
};

/**
 * @brief Returns true when the debug overlay renderer can target @p format.
 */
[[nodiscard]] bool supportsDebugOverlayFormat(opk::RawImagePixelFormat format) noexcept;

/**
 * @brief Validates and dispatches debug overlay rendering for a frame.
 *
 * This entry point is the handoff boundary from the GStreamer element into the
 * direct debug decoration implementation.
 */
[[nodiscard]] DebugOverlayStatus drawDebugOverlay(const DebugOverlayRequest &request) noexcept;

} // namespace opk::osd
