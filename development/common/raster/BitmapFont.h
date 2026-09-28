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
