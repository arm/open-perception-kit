/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

/**
 * @file BitmapFont.h
 * @brief Fixed bitmap font used by the lightweight raster painter.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace opk::raster {

/**
 * @brief One fixed-size monochrome glyph.
 *
 * Each row stores one bit per pixel. Bit 7 is the leftmost pixel.
 */
struct BitmapGlyph {
    /** @brief Packed glyph rows, with bit 7 representing the leftmost pixel. */
    std::array<std::byte, 12> rows{};
};

/**
 * @brief Tiny fixed ASCII bitmap font for debug overlays.
 */
class BitmapFont {
  public:
    /** @brief Glyph width in pixels before integer scaling. */
    static constexpr int GlyphWidth = 8;

    /** @brief Glyph height in pixels before integer scaling. */
    static constexpr int GlyphHeight = 12;

    /** @brief Horizontal gap between adjacent glyphs before integer scaling. */
    static constexpr int GlyphGap = 1;

    /**
     * @brief Returns true when @p character has a dedicated glyph.
     * @param character ASCII character to query.
     * @return True when the font contains a dedicated glyph for @p character.
     */
    static bool hasGlyph(char character) noexcept;

    /**
     * @brief Returns a glyph for @p character, or the '?' glyph when unsupported.
     * @param character ASCII character to look up.
     * @return Monochrome bitmap glyph for @p character.
     */
    static const BitmapGlyph &glyph(char character) noexcept;
};

} // namespace opk::raster
