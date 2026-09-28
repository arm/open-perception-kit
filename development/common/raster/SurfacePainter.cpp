/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/

#include "raster/SurfacePainter.h"

#include "opk/String.h"
#include "raster/BitmapFont.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace opk::raster {
namespace {

using I64 = std::int64_t;

constexpr I64 OutLeft = 1;
constexpr I64 OutRight = 2;
constexpr I64 OutBottom = 4;
constexpr I64 OutTop = 8;

struct Rect {
    I64 x = 0;
    I64 y = 0;
    I64 w = 0;
    I64 h = 0;
};

struct YuvCoefficients {
    float kr = 0.2126f;
    float kb = 0.0722f;
};

bool multiplyOverflows(std::size_t a, std::size_t b) noexcept {
    return a != 0 && b > std::numeric_limits<std::size_t>::max() / a;
}

bool hasPlaneBytes(const opk::ImagePlaneDesc &plane,
                   std::size_t rowBytes,
                   std::size_t height) noexcept {
    if (plane.mutableData == nullptr || rowBytes == 0 || height == 0) {
        return false;
    }

    const std::size_t strideBytes = plane.strideBytes != 0 ? plane.strideBytes : rowBytes;
    if (strideBytes < rowBytes) {
        return false;
    }

    if (plane.byteCount == 0) {
        return true;
    }

    const std::size_t lastRow = height - 1;
    if (multiplyOverflows(lastRow, strideBytes)) {
        return false;
    }

    const std::size_t lastRowOffset = lastRow * strideBytes;
    if (lastRowOffset > std::numeric_limits<std::size_t>::max() - rowBytes) {
        return false;
    }

    return plane.byteCount >= lastRowOffset + rowBytes;
}

std::uint8_t clampByte(float value) noexcept {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 255.0f)));
}

opk::YuvColorMatrix resolveMatrix(opk::YuvColorMatrix matrix, std::uint32_t height) noexcept {
    using enum opk::YuvColorMatrix;

    if (matrix != Unknown) {
        return matrix;
    }
    return height <= 576U ? Bt601 : Bt709;
}

opk::YuvRange resolveRange(opk::YuvRange range) noexcept {
    using enum opk::YuvRange;

    return range == Unknown ? Limited : range;
}

YuvCoefficients yuvCoefficients(opk::YuvColorMatrix matrix) noexcept {
    using enum opk::YuvColorMatrix;

    switch (matrix) {
    case Bt601:
        return {0.299f, 0.114f};
    case Bt2020:
        return {0.2627f, 0.0593f};
    case Bt709:
    case Unknown:
    default:
        return {};
    }
}

I64 normalizePositive(int value) noexcept {
    return value < 1 ? 1 : static_cast<I64>(value);
}

bool hasValidUtf8ContinuationBytes(const char *begin, const char *end) noexcept {
    for (const char *current = begin + 1; current < end; ++current) {
        const auto byte = static_cast<unsigned char>(*current);
        if ((byte & 0xC0U) != 0x80U) {
            return false;
        }
    }
    return true;
}

bool hasValidUtf8Range(char32_t codepoint, std::ptrdiff_t byteLength) noexcept {
    switch (byteLength) {
    case 1:
        return codepoint <= 0x7FU;
    case 2:
        return codepoint >= 0x80U && codepoint <= 0x7FFU;
    case 3:
        return codepoint >= 0x800U && codepoint <= 0xFFFFU &&
               !(codepoint >= 0xD800U && codepoint <= 0xDFFFU);
    case 4:
        return codepoint >= 0x10000U && codepoint <= 0x10FFFFU;
    default:
        return false;
    }
}

template <typename Emit> void emitAsciiGlyph(char character, Emit &emit) noexcept {
    switch (character) {
    case '\n':
    case '\r':
    case '\t':
        emit(' ');
        break;
    default:
        emit(BitmapFont::hasGlyph(character) ? character : '?');
        break;
    }
}

template <typename Emit> void emitSimplifiedGlyphs(char32_t codepoint, Emit &emit) noexcept {
    switch (codepoint) {
    case U'\u00A0':
        emitAsciiGlyph(' ', emit);
        break;
    case U'\u2010':
    case U'\u2011':
    case U'\u2012':
    case U'\u2013':
    case U'\u2014':
    case U'\u2212':
        emitAsciiGlyph('-', emit);
        break;
    case U'\u2018':
    case U'\u2019':
    case U'\u201A':
    case U'\u2032':
        emitAsciiGlyph('\'', emit);
        break;
    case U'\u201C':
    case U'\u201D':
    case U'\u201E':
    case U'\u2033':
        emitAsciiGlyph('"', emit);
        break;
    case U'\u2026':
        emitAsciiGlyph('.', emit);
        emitAsciiGlyph('.', emit);
        emitAsciiGlyph('.', emit);
        break;
    default:
        if (codepoint <= 0x7FU) {
            emitAsciiGlyph(static_cast<char>(codepoint), emit);
        } else {
            emitAsciiGlyph('?', emit);
        }
        break;
    }
}

template <typename Emit> void forEachTextGlyph(std::string_view text, Emit emit) noexcept {
    const char *current = text.data();
    const char *end = current + text.size();

    while (current < end) {
        const char *next = opk::codepoint::skip(current);
        if (next <= current || next > end) {
            emitAsciiGlyph('?', emit);
            break;
        }

        const auto byteLength = next - current;
        const char32_t codepoint = opk::codepoint::decode(current);
        if (!hasValidUtf8ContinuationBytes(current, next) ||
            !hasValidUtf8Range(codepoint, byteLength)) {
            emitAsciiGlyph('?', emit);
            current = next;
            continue;
        }

        emitSimplifiedGlyphs(codepoint, emit);
        current = next;
    }
}

SurfacePainter::TextMetrics measureTextGlyphs(std::string_view text, I64 normalizedScale) noexcept {
    const I64 glyphWidth = BitmapFont::GlyphWidth * normalizedScale;
    const I64 glyphHeight = BitmapFont::GlyphHeight * normalizedScale;
    const I64 glyphGap = BitmapFont::GlyphGap * normalizedScale;
    const I64 glyphAdvance = glyphWidth + glyphGap;
    const auto maxLength = static_cast<std::size_t>(std::numeric_limits<I64>::max() / glyphAdvance);

    std::size_t textLength = 0;
    bool overflow = false;
    forEachTextGlyph(text, [&](char) noexcept {
        if (textLength >= maxLength) {
            overflow = true;
            return;
        }
        ++textLength;
    });

    if (textLength == 0 || overflow) {
        return {};
    }

    const auto textLengthPixels = static_cast<I64>(textLength);
    return {
        textLengthPixels * glyphWidth + (textLengthPixels - 1) * glyphGap,
        glyphHeight,
    };
}

std::uint32_t bgraWord(const std::uint8_t b, const std::uint8_t g, const std::uint8_t r) noexcept {
    return (std::uint32_t{255} << 24) | (static_cast<std::uint32_t>(r) << 16) |
           (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
}

bool clipRect(Rect &rect, I64 width, I64 height) noexcept {
    if (rect.w <= 0 || rect.h <= 0 || width <= 0 || height <= 0) {
        return false;
    }

    const I64 x0 = std::clamp(rect.x, I64{0}, width);
    const I64 y0 = std::clamp(rect.y, I64{0}, height);
    const I64 x1 = std::clamp(rect.x + rect.w, I64{0}, width);
    const I64 y1 = std::clamp(rect.y + rect.h, I64{0}, height);
    if (x1 <= x0 || y1 <= y0) {
        return false;
    }

    rect = {x0, y0, x1 - x0, y1 - y0};
    return true;
}

I64 outCode(I64 x, I64 y, I64 width, I64 height) noexcept {
    I64 code = 0;
    if (x < 0) {
        code |= OutLeft;
    } else if (x >= width) {
        code |= OutRight;
    }
    if (y < 0) {
        code |= OutTop;
    } else if (y >= height) {
        code |= OutBottom;
    }
    return code;
}

bool clipLineToSurface( // NOSONAR: Cohen-Sutherland clipping is clearer kept as one loop.
    I64 &x0,
    I64 &y0,
    I64 &x1,
    I64 &y1,
    I64 width,
    I64 height) noexcept {
    if (width <= 0 || height <= 0) {
        return false;
    }

    I64 code0 = outCode(x0, y0, width, height);
    I64 code1 = outCode(x1, y1, width, height);
    const long double xmin = 0.0L;
    const long double ymin = 0.0L;
    const auto xmax = static_cast<long double>(width - 1);
    const auto ymax = static_cast<long double>(height - 1);

    while (true) {
        if ((code0 | code1) == 0) {
            return true;
        }
        if ((code0 & code1) != 0) {
            return false;
        }

        const I64 codeOut = code0 != 0 ? code0 : code1;
        long double x = 0.0L;
        long double y = 0.0L;
        const auto fx0 = static_cast<long double>(x0);
        const auto fy0 = static_cast<long double>(y0);
        const auto fx1 = static_cast<long double>(x1);
        const auto fy1 = static_cast<long double>(y1);

        if ((codeOut & OutTop) != 0) {
            if (y1 == y0) {
                return false;
            }
            x = fx0 + (fx1 - fx0) * (ymin - fy0) / (fy1 - fy0);
            y = ymin;
        } else if ((codeOut & OutBottom) != 0) {
            if (y1 == y0) {
                return false;
            }
            x = fx0 + (fx1 - fx0) * (ymax - fy0) / (fy1 - fy0);
            y = ymax;
        } else if ((codeOut & OutRight) != 0) {
            if (x1 == x0) {
                return false;
            }
            y = fy0 + (fy1 - fy0) * (xmax - fx0) / (fx1 - fx0);
            x = xmax;
        } else {
            if (x1 == x0) {
                return false;
            }
            y = fy0 + (fy1 - fy0) * (xmin - fx0) / (fx1 - fx0);
            x = xmin;
        }

        if (codeOut == code0) {
            x0 = static_cast<I64>(std::llround(x));
            y0 = static_cast<I64>(std::llround(y));
            code0 = outCode(x0, y0, width, height);
        } else {
            x1 = static_cast<I64>(std::llround(x));
            y1 = static_cast<I64>(std::llround(y));
            code1 = outCode(x1, y1, width, height);
        }
    }
}

I64 isqrt(long double value) noexcept {
    if (value <= 0.0L) {
        return 0;
    }
    return static_cast<I64>(std::sqrt(value));
}

} // namespace

SurfacePainter::SurfacePainter(opk::RawImagePixelFormat format,
                               std::uint32_t width,
                               std::uint32_t height,
                               std::span<opk::ImagePlaneDesc> planes,
                               opk::YuvColorMatrix matrix,
                               opk::YuvRange range) noexcept
    : targetFormat(format), surfaceWidth(width), surfaceHeight(height),
      targetPlaneCount(std::min<std::size_t>(planes.size(), targetPlanes.size())),
      yuvMatrix(resolveMatrix(matrix, height)), yuvRange(resolveRange(range)) {
    for (std::size_t index = 0; index < targetPlaneCount; ++index) {
        targetPlanes[index] = planes[index];
    }

    isValid = validate();
}

bool SurfacePainter::valid() const noexcept {
    return isValid;
}

SurfacePainter::TextMetrics SurfacePainter::measureText(std::string_view text, int scale) noexcept {
    if (text.empty()) {
        return {};
    }

    return measureTextGlyphs(text, normalizePositive(scale));
}

bool SurfacePainter::validate() noexcept {
    if (surfaceWidth == 0 || surfaceHeight == 0) {
        return false;
    }

    using enum opk::RawImagePixelFormat;

    const auto width = static_cast<std::size_t>(surfaceWidth);
    const auto height = static_cast<std::size_t>(surfaceHeight);
    switch (targetFormat) {
    case Bgra:
        return targetPlaneCount >= 1 && width <= std::numeric_limits<std::size_t>::max() / 4 &&
               hasPlaneBytes(targetPlanes[0], width * 4, height);
    case Rgb:
        return targetPlaneCount >= 1 && width <= std::numeric_limits<std::size_t>::max() / 3 &&
               hasPlaneBytes(targetPlanes[0], width * 3, height);
    case I420: {
        const std::size_t chromaWidth = (width + 1) / 2;
        const std::size_t chromaHeight = (height + 1) / 2;
        return targetPlaneCount >= 3 && hasPlaneBytes(targetPlanes[0], width, height) &&
               hasPlaneBytes(targetPlanes[1], chromaWidth, chromaHeight) &&
               hasPlaneBytes(targetPlanes[2], chromaWidth, chromaHeight);
    }
    case Nv12: {
        const std::size_t chromaWidth = (width + 1) / 2;
        const std::size_t chromaHeight = (height + 1) / 2;
        return targetPlaneCount >= 2 && hasPlaneBytes(targetPlanes[0], width, height) &&
               !multiplyOverflows(chromaWidth, std::size_t{2}) &&
               hasPlaneBytes(targetPlanes[1], chromaWidth * 2, chromaHeight);
    }
    case Yuy2: {
        const std::size_t rowBytes = ((width + 1) / 2) * 4;
        return targetPlaneCount >= 1 && hasPlaneBytes(targetPlanes[0], rowBytes, height);
    }
    default:
        return false;
    }
}

SurfacePainter::TargetColor SurfacePainter::makeTargetColor(opk::Color color) const noexcept {
    TargetColor target;
    target.r = opk::Colors::getRed(color);
    target.g = opk::Colors::getGreen(color);
    target.b = opk::Colors::getBlue(color);

    const float r = static_cast<float>(target.r) / 255.0f;
    const float g = static_cast<float>(target.g) / 255.0f;
    const float b = static_cast<float>(target.b) / 255.0f;
    const auto coefficients = yuvCoefficients(yuvMatrix);
    const float kr = coefficients.kr;
    const float kb = coefficients.kb;
    const float kg = 1.0f - kr - kb;
    const float y = kr * r + kg * g + kb * b;
    const float cb = (b - y) / (2.0f * (1.0f - kb));
    const float cr = (r - y) / (2.0f * (1.0f - kr));

    using enum opk::YuvRange;

    if (yuvRange == Full) {
        target.y = clampByte(y * 255.0f);
        target.u = clampByte(cb * 255.0f + 128.0f);
        target.v = clampByte(cr * 255.0f + 128.0f);
    } else {
        target.y = clampByte(y * 219.0f + 16.0f);
        target.u = clampByte(cb * 224.0f + 128.0f);
        target.v = clampByte(cr * 224.0f + 128.0f);
    }
    return target;
}

void SurfacePainter::fillClippedSpan( // NOSONAR: explicit per-format stores keep the raster hot
                                      // path direct.
    I64 x0,
    I64 x1,
    I64 y,
    const TargetColor &color) noexcept {
    if (!isValid || y < 0 || y >= static_cast<I64>(surfaceHeight) || x1 <= x0) {
        return;
    }

    x0 = std::clamp<I64>(x0, 0, surfaceWidth);
    x1 = std::clamp<I64>(x1, 0, surfaceWidth);
    if (x1 <= x0) {
        return;
    }

    using enum opk::RawImagePixelFormat;

    switch (targetFormat) {
    case Bgra: {
        const std::size_t stride =
            targetPlanes[0].strideBytes != 0 ? targetPlanes[0].strideBytes : surfaceWidth * 4;
        auto *row = targetPlanes[0].mutableData + static_cast<std::size_t>(y) * stride;
        auto *first = row + static_cast<std::size_t>(x0) * 4;
        const auto pixelCount = static_cast<std::size_t>(x1 - x0);
        if constexpr (std::endian::native == std::endian::little) {
            const auto firstAddress = reinterpret_cast<std::uintptr_t>(first); // NOSONAR
            if (firstAddress % alignof(std::uint32_t) == 0U) {
                auto *words = reinterpret_cast<std::uint32_t *>(first); // NOSONAR
                std::fill_n(words, pixelCount, bgraWord(color.b, color.g, color.r));
                break;
            }
        }
        for (I64 x = x0; x < x1; ++x) {
            auto *pixel = row + static_cast<std::size_t>(x) * 4;
            pixel[0] = color.b;
            pixel[1] = color.g;
            pixel[2] = color.r;
            pixel[3] = 255;
        }
        break;
    }
    case Rgb: {
        const std::size_t stride =
            targetPlanes[0].strideBytes != 0 ? targetPlanes[0].strideBytes : surfaceWidth * 3;
        auto *row = targetPlanes[0].mutableData + static_cast<std::size_t>(y) * stride;
        auto *first = row + static_cast<std::size_t>(x0) * 3;
        if (color.r == color.g && color.g == color.b) {
            std::memset(first, color.r, static_cast<std::size_t>(x1 - x0) * 3U);
            break;
        }
        for (I64 x = x0; x < x1; ++x) {
            auto *pixel = row + static_cast<std::size_t>(x) * 3;
            pixel[0] = color.r;
            pixel[1] = color.g;
            pixel[2] = color.b;
        }
        break;
    }
    case I420: {
        const std::size_t yStride =
            targetPlanes[0].strideBytes != 0 ? targetPlanes[0].strideBytes : surfaceWidth;
        const std::size_t chromaWidth = (surfaceWidth + 1) / 2;
        const std::size_t uStride =
            targetPlanes[1].strideBytes != 0 ? targetPlanes[1].strideBytes : chromaWidth;
        const std::size_t vStride =
            targetPlanes[2].strideBytes != 0 ? targetPlanes[2].strideBytes : chromaWidth;
        auto *yRow = targetPlanes[0].mutableData + static_cast<std::size_t>(y) * yStride;
        const auto xStart = static_cast<std::size_t>(x0);
        const auto xEnd = static_cast<std::size_t>(x1);
        std::fill(yRow + xStart, yRow + xEnd, color.y);
        const I64 cx0 = x0 / 2;
        const I64 cx1 = (x1 - 1) / 2 + 1;
        const auto cy = static_cast<std::size_t>(y / 2);
        const auto cxStart = static_cast<std::size_t>(cx0);
        const auto cxEnd = static_cast<std::size_t>(cx1);
        auto *uRow = targetPlanes[1].mutableData + cy * uStride;
        auto *vRow = targetPlanes[2].mutableData + cy * vStride;
        std::fill(uRow + cxStart, uRow + cxEnd, color.u);
        std::fill(vRow + cxStart, vRow + cxEnd, color.v);
        break;
    }
    case Nv12: {
        const std::size_t yStride =
            targetPlanes[0].strideBytes != 0 ? targetPlanes[0].strideBytes : surfaceWidth;
        const std::size_t chromaWidth = (surfaceWidth + 1) / 2;
        const std::size_t uvStride =
            targetPlanes[1].strideBytes != 0 ? targetPlanes[1].strideBytes : chromaWidth * 2;
        auto *yRow = targetPlanes[0].mutableData + static_cast<std::size_t>(y) * yStride;
        const auto xStart = static_cast<std::size_t>(x0);
        const auto xEnd = static_cast<std::size_t>(x1);
        std::fill(yRow + xStart, yRow + xEnd, color.y);
        const I64 cx0 = x0 / 2;
        const I64 cx1 = (x1 - 1) / 2 + 1;
        auto *uvRow = targetPlanes[1].mutableData + static_cast<std::size_t>(y / 2) * uvStride;
        const auto cxStart = static_cast<std::size_t>(cx0);
        const auto cxEnd = static_cast<std::size_t>(cx1);
        for (std::size_t cx = cxStart; cx < cxEnd; ++cx) {
            auto *uv = uvRow + cx * 2;
            uv[0] = color.u;
            uv[1] = color.v;
        }
        break;
    }
    case Yuy2: {
        const std::size_t stride = targetPlanes[0].strideBytes != 0 ? targetPlanes[0].strideBytes
                                                                    : ((surfaceWidth + 1) / 2) * 4;
        auto *row = targetPlanes[0].mutableData + static_cast<std::size_t>(y) * stride;
        const I64 pair0 = x0 / 2;
        const I64 pair1 = (x1 - 1) / 2 + 1;
        for (I64 pair = pair0; pair < pair1; ++pair) {
            auto *pixel = row + static_cast<std::size_t>(pair) * 4;
            const I64 evenX = pair * 2;
            const I64 oddX = evenX + 1;
            if (evenX >= x0 && evenX < x1) {
                pixel[0] = color.y;
            }
            pixel[1] = color.u;
            if (oddX >= x0 && oddX < x1 && oddX < static_cast<I64>(surfaceWidth)) {
                pixel[2] = color.y;
            }
            pixel[3] = color.v;
        }
        break;
    }
    default:
        break;
    }
}

void SurfacePainter::fillClippedRect(
    I64 x, I64 y, I64 w, I64 h, const TargetColor &color) noexcept {
    Rect rect{x, y, w, h};
    if (!clipRect(rect, surfaceWidth, surfaceHeight)) {
        return;
    }

    using enum opk::RawImagePixelFormat;

    switch (targetFormat) {
    case I420: {
        const std::size_t yStride =
            targetPlanes[0].strideBytes != 0 ? targetPlanes[0].strideBytes : surfaceWidth;
        const auto xStart = static_cast<std::size_t>(rect.x);
        const auto xEnd = static_cast<std::size_t>(rect.x + rect.w);
        for (I64 row = rect.y; row < rect.y + rect.h; ++row) {
            auto *yRow = targetPlanes[0].mutableData + static_cast<std::size_t>(row) * yStride;
            std::fill(yRow + xStart, yRow + xEnd, color.y);
        }

        const std::size_t chromaWidth = (surfaceWidth + 1) / 2;
        const std::size_t uStride =
            targetPlanes[1].strideBytes != 0 ? targetPlanes[1].strideBytes : chromaWidth;
        const std::size_t vStride =
            targetPlanes[2].strideBytes != 0 ? targetPlanes[2].strideBytes : chromaWidth;
        const I64 cx0 = rect.x / 2;
        const I64 cx1 = (rect.x + rect.w - 1) / 2 + 1;
        const I64 cy0 = rect.y / 2;
        const I64 cy1 = (rect.y + rect.h - 1) / 2 + 1;
        const auto cxStart = static_cast<std::size_t>(cx0);
        const auto cxEnd = static_cast<std::size_t>(cx1);
        for (I64 cy = cy0; cy < cy1; ++cy) {
            auto *uRow = targetPlanes[1].mutableData + static_cast<std::size_t>(cy) * uStride;
            auto *vRow = targetPlanes[2].mutableData + static_cast<std::size_t>(cy) * vStride;
            std::fill(uRow + cxStart, uRow + cxEnd, color.u);
            std::fill(vRow + cxStart, vRow + cxEnd, color.v);
        }
        return;
    }
    case Nv12: {
        const std::size_t yStride =
            targetPlanes[0].strideBytes != 0 ? targetPlanes[0].strideBytes : surfaceWidth;
        const auto xStart = static_cast<std::size_t>(rect.x);
        const auto xEnd = static_cast<std::size_t>(rect.x + rect.w);
        for (I64 row = rect.y; row < rect.y + rect.h; ++row) {
            auto *yRow = targetPlanes[0].mutableData + static_cast<std::size_t>(row) * yStride;
            std::fill(yRow + xStart, yRow + xEnd, color.y);
        }

        const std::size_t chromaWidth = (surfaceWidth + 1) / 2;
        const std::size_t uvStride =
            targetPlanes[1].strideBytes != 0 ? targetPlanes[1].strideBytes : chromaWidth * 2;
        const I64 cx0 = rect.x / 2;
        const I64 cx1 = (rect.x + rect.w - 1) / 2 + 1;
        const I64 cy0 = rect.y / 2;
        const I64 cy1 = (rect.y + rect.h - 1) / 2 + 1;
        const auto cxStart = static_cast<std::size_t>(cx0);
        const auto cxEnd = static_cast<std::size_t>(cx1);
        for (I64 cy = cy0; cy < cy1; ++cy) {
            auto *uvRow = targetPlanes[1].mutableData + static_cast<std::size_t>(cy) * uvStride;
            for (std::size_t cx = cxStart; cx < cxEnd; ++cx) {
                auto *uv = uvRow + cx * 2;
                uv[0] = color.u;
                uv[1] = color.v;
            }
        }
        return;
    }
    default:
        break;
    }

    for (I64 row = rect.y; row < rect.y + rect.h; ++row) {
        fillClippedSpan(rect.x, rect.x + rect.w, row, color);
    }
}

void SurfacePainter::drawOnePixelRect(
    I64 x, I64 y, I64 w, I64 h, const TargetColor &color) noexcept {
    if (w <= 0 || h <= 0) {
        return;
    }

    fillClippedRect(x, y, w, 1, color);
    if (h > 1) {
        fillClippedRect(x, y + h - 1, w, 1, color);
    }
    if (h > 2) {
        fillClippedRect(x, y + 1, 1, h - 2, color);
        if (w > 1) {
            fillClippedRect(x + w - 1, y + 1, 1, h - 2, color);
        }
    }
}

void SurfacePainter::fillRect(int x, int y, int w, int h, opk::Color color) noexcept {
    if (!isValid || w <= 0 || h <= 0) {
        return;
    }

    fillClippedRect(x, y, w, h, makeTargetColor(color));
}

void SurfacePainter::drawPoint(int x, int y, opk::Color color, int size) noexcept {
    if (!isValid) {
        return;
    }

    const I64 normalizedSize = normalizePositive(size);
    fillClippedRect(static_cast<I64>(x) - (normalizedSize / 2),
                    static_cast<I64>(y) - (normalizedSize / 2),
                    normalizedSize,
                    normalizedSize,
                    makeTargetColor(color));
}

void SurfacePainter::drawRect(
    int x, int y, int w, int h, opk::Color color, int thickness) noexcept {
    if (!isValid || w <= 0 || h <= 0) {
        return;
    }

    const TargetColor targetColor = makeTargetColor(color);
    const I64 t = normalizePositive(thickness);
    for (I64 layer = 0; layer < t; ++layer) {
        I64 offset = 0;
        if (layer != 0) {
            if (layer % 2 == 1) {
                offset = (layer + 1) / 2;
            } else {
                offset = -(layer / 2);
            }
        }
        drawOnePixelRect(static_cast<I64>(x) + offset,
                         static_cast<I64>(y) + offset,
                         static_cast<I64>(w) - 2 * offset,
                         static_cast<I64>(h) - 2 * offset,
                         targetColor);
    }
}

void SurfacePainter::drawLine(
    int x0, int y0, int x1, int y1, opk::Color color, int thickness) noexcept {
    if (!isValid) {
        return;
    }

    I64 ax = x0;
    I64 ay = y0;
    I64 bx = x1;
    I64 by = y1;
    if (!clipLineToSurface(ax, ay, bx, by, surfaceWidth, surfaceHeight)) {
        return;
    }

    const TargetColor targetColor = makeTargetColor(color);
    const I64 t = normalizePositive(thickness);
    const I64 halfBefore = (t - 1) / 2;
    const I64 halfAfter = t / 2;
    const I64 dx = std::abs(bx - ax);
    const I64 sx = ax < bx ? 1 : -1;
    const I64 dy = -std::abs(by - ay);
    const I64 sy = ay < by ? 1 : -1;
    I64 error = dx + dy;

    while (true) {
        fillClippedRect(
            ax - halfBefore, ay - halfBefore, halfBefore + halfAfter + 1, t, targetColor);
        if (ax == bx && ay == by) {
            break;
        }
        const I64 e2 = 2 * error;
        if (e2 >= dy) {
            error += dy;
            ax += sx;
        }
        if (e2 <= dx) {
            error += dx;
            ay += sy;
        }
    }
}

void SurfacePainter::drawCircle(
    int cx, int cy, int radius, opk::Color color, int thickness) noexcept {
    if (!isValid || radius <= 0) {
        return;
    }

    const I64 r = radius;
    const I64 t = normalizePositive(thickness);
    const I64 inwardLayers = t / 2;
    const I64 outwardLayers = (t - 1) / 2;
    const bool filled = inwardLayers >= r;
    const I64 outer = r + (filled ? t / 2 : outwardLayers);
    const I64 innerBoundary = filled ? 0 : std::max<I64>(0, r - inwardLayers - 1);
    const I64 yMin = std::max<I64>(static_cast<I64>(cy) - outer, 0);
    const I64 yMax =
        std::min<I64>(static_cast<I64>(cy) + outer, static_cast<I64>(surfaceHeight) - 1);
    if (yMax < yMin) {
        return;
    }

    const TargetColor targetColor = makeTargetColor(color);
    const long double outer2 = static_cast<long double>(outer) * static_cast<long double>(outer);
    const long double inner2 =
        static_cast<long double>(innerBoundary) * static_cast<long double>(innerBoundary);
    for (I64 y = yMin; y <= yMax; ++y) {
        const I64 dy = y - static_cast<I64>(cy);
        const long double dy2 = static_cast<long double>(dy) * static_cast<long double>(dy);
        const I64 outerX = isqrt(outer2 - dy2);
        const I64 absDy = dy < 0 ? -dy : dy;
        if (filled || (innerBoundary > 0 && absDy >= innerBoundary)) {
            fillClippedSpan(
                static_cast<I64>(cx) - outerX, static_cast<I64>(cx) + outerX + 1, y, targetColor);
        } else {
            const I64 innerX = isqrt(inner2 - dy2);
            fillClippedSpan(
                static_cast<I64>(cx) - outerX, static_cast<I64>(cx) - innerX, y, targetColor);
            fillClippedSpan(static_cast<I64>(cx) + innerX + 1,
                            static_cast<I64>(cx) + outerX + 1,
                            y,
                            targetColor);
        }
    }
}

void SurfacePainter::drawText(int x,
                              int y,
                              std::string_view text,
                              opk::Color fontColor,
                              opk::Color backgroundColor,
                              int scale,
                              TextAnchor anchor) noexcept {
    if (!isValid || text.empty()) {
        return;
    }

    const I64 normalizedScale = normalizePositive(scale);
    const auto metrics = measureTextGlyphs(text, normalizedScale);
    if (metrics.width <= 0 || metrics.height <= 0) {
        return;
    }

    const I64 glyphWidth = BitmapFont::GlyphWidth * normalizedScale;
    const I64 glyphGap = BitmapFont::GlyphGap * normalizedScale;
    const I64 textWidth = metrics.width;
    const I64 textHeight = metrics.height;
    I64 originX = x;
    I64 originY = y;

    using enum TextAnchor;

    switch (anchor) {
    case TopRight:
        originX -= textWidth;
        break;
    case BottomLeft:
        originY -= textHeight;
        break;
    case BottomRight:
        originX -= textWidth;
        originY -= textHeight;
        break;
    case Center:
        originX -= textWidth / 2;
        originY -= textHeight / 2;
        break;
    case TopLeft:
    default:
        break;
    }

    fillClippedRect(originX, originY, textWidth, textHeight, makeTargetColor(backgroundColor));
    const TargetColor glyphColor = makeTargetColor(fontColor);

    I64 penX = originX;
    forEachTextGlyph(text, [&](char rawCharacter) noexcept {
        const BitmapGlyph &glyph = BitmapFont::glyph(rawCharacter);
        for (int gy = 0; gy < BitmapFont::GlyphHeight; ++gy) {
            const auto row = glyph.rows[gy];
            for (int gx = 0; gx < BitmapFont::GlyphWidth; ++gx) {
                const auto mask = static_cast<std::byte>(1U << (7 - gx));
                if ((row & mask) == std::byte{}) {
                    continue;
                }
                fillClippedRect(penX + static_cast<I64>(gx) * normalizedScale,
                                originY + static_cast<I64>(gy) * normalizedScale,
                                normalizedScale,
                                normalizedScale,
                                glyphColor);
            }
        }
        penX += glyphWidth + glyphGap;
    });
}

} // namespace opk::raster
