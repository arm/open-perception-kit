/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

/**
 * @file SurfacePainter.h
 * @brief Lightweight raster drawing on caller-owned image planes.
 */

#pragma once

#include "opk/Color.h"
#include "opk/ImageOpDesc.h"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace opk::raster {

/**
 * @brief Anchor point used to position text bounding boxes.
 */
enum class TextAnchor {
    /** Position text from the upper-left corner of its bounding box. */
    TopLeft,

    /** Position text from the upper-right corner of its bounding box. */
    TopRight,

    /** Position text from the lower-left corner of its bounding box. */
    BottomLeft,

    /** Position text from the lower-right corner of its bounding box. */
    BottomRight,

    /** Position text from the center of its bounding box. */
    Center,
};

/**
 * @brief Synchronous non-owning painter for raw image memory.
 *
 * SurfacePainter borrows writable image planes from its caller. It does not own,
 * allocate, throw, blend, or retain drawing commands. Invalid painters and invalid
 * draw requests silently do nothing.
 */
class SurfacePainter {
  public:
    /**
     * @brief Pixel dimensions occupied by bitmap-font text.
     */
    struct TextMetrics {
        /** @brief Width of the rendered text bounding box in pixels. */
        std::int64_t width = 0;

        /** @brief Height of the rendered text bounding box in pixels. */
        std::int64_t height = 0;
    };

    /**
     * @brief Creates a painter over caller-owned image planes.
     * @param format Raw pixel format of the writable surface.
     * @param width Surface width in pixels.
     * @param height Surface height in pixels.
     * @param planes Writable plane descriptors borrowed for the painter lifetime.
     * @param matrix YUV color matrix used when painting YUV surfaces.
     * @param range YUV numeric range used when painting YUV surfaces.
     *
     * The constructor validates plane count, writable pointers, stride, and byte
     * capacity. Invalid input produces an invalid painter instead of throwing.
     */
    SurfacePainter(opk::RawImagePixelFormat format,
                   std::uint32_t width,
                   std::uint32_t height,
                   std::span<opk::ImagePlaneDesc> planes,
                   opk::YuvColorMatrix matrix = opk::YuvColorMatrix::Unknown,
                   opk::YuvRange range = opk::YuvRange::Unknown) noexcept;

    /**
     * @brief Returns true when the target format and planes are writable and valid.
     * @return True when subsequent draw calls can write into the borrowed surface.
     */
    [[nodiscard]] bool valid() const noexcept;

    /**
     * @brief Measures bitmap-font text without touching a surface.
     * @param text UTF-8 text to measure after ASCII bitmap-font simplification.
     * @param scale Integer glyph scale; values below 1 are treated as 1.
     * @return Bounding-box dimensions in pixels, or zeros when the request overflows.
     */
    [[nodiscard]] static TextMetrics measureText(std::string_view text, int scale = 1) noexcept;

    /**
     * @brief Fills an opaque rectangle.
     * @param x Left edge in surface coordinates.
     * @param y Top edge in surface coordinates.
     * @param w Rectangle width in pixels.
     * @param h Rectangle height in pixels.
     * @param color Opaque color to draw.
     */
    void fillRect(int x, int y, int w, int h, opk::Color color) noexcept;

    /**
     * @brief Draws an opaque square point centered on a surface coordinate.
     * @param x Point center X coordinate.
     * @param y Point center Y coordinate.
     * @param color Opaque color to draw.
     * @param size Point size in pixels; values below 1 are treated as 1.
     */
    void drawPoint(int x, int y, opk::Color color, int size = 1) noexcept;

    /**
     * @brief Draws an opaque rectangle outline.
     * @param x Left edge in surface coordinates.
     * @param y Top edge in surface coordinates.
     * @param w Rectangle width in pixels.
     * @param h Rectangle height in pixels.
     * @param color Opaque color to draw.
     * @param thickness Stroke thickness in pixels; values below 1 are treated as 1.
     */
    void drawRect(int x, int y, int w, int h, opk::Color color, int thickness = 1) noexcept;

    /**
     * @brief Draws an opaque line.
     * @param x0 Start X coordinate.
     * @param y0 Start Y coordinate.
     * @param x1 End X coordinate.
     * @param y1 End Y coordinate.
     * @param color Opaque color to draw.
     * @param thickness Stroke thickness in pixels; values below 1 are treated as 1.
     */
    void drawLine(int x0, int y0, int x1, int y1, opk::Color color, int thickness = 1) noexcept;

    /**
     * @brief Draws an opaque circle outline.
     * @param cx Circle center X coordinate.
     * @param cy Circle center Y coordinate.
     * @param radius Circle radius in pixels.
     * @param color Opaque color to draw.
     * @param thickness Stroke thickness in pixels; values below 1 are treated as 1.
     */
    void drawCircle(int cx, int cy, int radius, opk::Color color, int thickness = 1) noexcept;

    /**
     * @brief Draws opaque bitmap-font text with an opaque background.
     * @param x Anchor X coordinate.
     * @param y Anchor Y coordinate.
     * @param text UTF-8 text to draw after ASCII bitmap-font simplification.
     * @param fontColor Opaque glyph color.
     * @param backgroundColor Opaque background color for the text bounding box.
     * @param scale Integer glyph scale; values below 1 are treated as 1.
     * @param anchor Anchor point used to position the text bounding box.
     */
    void drawText(int x,
                  int y,
                  std::string_view text,
                  opk::Color fontColor,
                  opk::Color backgroundColor,
                  int scale = 1,
                  TextAnchor anchor = TextAnchor::TopLeft) noexcept;

  private:
    /**
     * @brief Format-specific packed color used by the low-level fill routines.
     */
    struct TargetColor {
        /** Red channel used by RGB/BGRA targets. */
        std::uint8_t r = 0;

        /** Green channel used by RGB/BGRA targets. */
        std::uint8_t g = 0;

        /** Blue channel used by RGB/BGRA targets. */
        std::uint8_t b = 0;

        /** Luma value used by YUV targets. */
        std::uint8_t y = 0;

        /** U/Cb chroma value used by YUV targets. */
        std::uint8_t u = 128;

        /** V/Cr chroma value used by YUV targets. */
        std::uint8_t v = 128;
    };

    /** @brief Validates surface dimensions, format, plane count, pointers, and strides. */
    bool validate() noexcept;

    /** @brief Converts an RGB color to the packed target representation. */
    TargetColor makeTargetColor(opk::Color color) const noexcept;

    /** @brief Clips and fills a rectangle on the target surface. */
    void fillClippedRect(std::int64_t x,
                         std::int64_t y,
                         std::int64_t w,
                         std::int64_t h,
                         const TargetColor &color) noexcept;

    /** @brief Clips and fills one horizontal span on the target surface. */
    void fillClippedSpan(std::int64_t x0,
                         std::int64_t x1,
                         std::int64_t y,
                         const TargetColor &color) noexcept;

    /** @brief Draws a one-pixel rectangle outline before stroke expansion. */
    void drawOnePixelRect(std::int64_t x,
                          std::int64_t y,
                          std::int64_t w,
                          std::int64_t h,
                          const TargetColor &color) noexcept;

    /** Raw pixel format of the target surface. */
    opk::RawImagePixelFormat targetFormat = opk::RawImagePixelFormat::Unknown;

    /** Target surface width in pixels. */
    std::uint32_t surfaceWidth = 0;

    /** Target surface height in pixels. */
    std::uint32_t surfaceHeight = 0;

    /** Copied borrowed plane descriptors. */
    std::array<opk::ImagePlaneDesc, opk::MaxImagePlaneCount> targetPlanes{};

    /** Number of valid entries in @ref targetPlanes. */
    std::size_t targetPlaneCount = 0;

    /** YUV matrix used for RGB-to-YUV drawing on YUV targets. */
    opk::YuvColorMatrix yuvMatrix = opk::YuvColorMatrix::Unknown;

    /** YUV range used for RGB-to-YUV drawing on YUV targets. */
    opk::YuvRange yuvRange = opk::YuvRange::Unknown;

    /** Cached validation result. */
    bool isValid = false;
};

} // namespace opk::raster
