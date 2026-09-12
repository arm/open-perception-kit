/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#include "raster/SegmentationMask.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace pek::raster {
namespace {

struct YuvCoefficients {
    float kr = 0.2126f;
    float kb = 0.0722f;
};

struct TargetColor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t y = 0;
    std::uint8_t u = 128;
    std::uint8_t v = 128;
};

struct RgbBytes {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

struct BitmapView {
    const std::uint8_t *data = nullptr;
    std::size_t width = 0;
    std::size_t height = 0;
};

struct SurfaceLayout {
    pek::RawImagePixelFormat format = pek::RawImagePixelFormat::Unknown;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::span<pek::ImagePlaneDesc> planes{};
    pek::YuvColorMatrix yuvMatrix = pek::YuvColorMatrix::Unknown;
    pek::YuvRange yuvRange = pek::YuvRange::Unknown;
    std::size_t plane0Stride = 0;
    std::size_t plane1Stride = 0;
    std::size_t plane2Stride = 0;
};

bool multiplyOverflows(std::size_t a, std::size_t b) noexcept {
    return a != 0 && b > std::numeric_limits<std::size_t>::max() / a;
}

bool hasPlaneBytes(const pek::ImagePlaneDesc &plane,
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

pek::YuvColorMatrix resolveMatrix(pek::YuvColorMatrix matrix, std::uint32_t height) noexcept {
    using enum pek::YuvColorMatrix;

    if (matrix != Unknown) {
        return matrix;
    }
    return height <= 576U ? Bt601 : Bt709;
}

pek::YuvRange resolveRange(pek::YuvRange range) noexcept {
    using enum pek::YuvRange;

    return range == Unknown ? Limited : range;
}

YuvCoefficients yuvCoefficients(pek::YuvColorMatrix matrix) noexcept {
    using enum pek::YuvColorMatrix;

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

TargetColor
makeTargetColor(RgbBytes color, pek::YuvColorMatrix matrix, pek::YuvRange range) noexcept {
    TargetColor target;
    target.r = color.r;
    target.g = color.g;
    target.b = color.b;

    const float r = static_cast<float>(target.r) / 255.0f;
    const float g = static_cast<float>(target.g) / 255.0f;
    const float b = static_cast<float>(target.b) / 255.0f;
    const auto coefficients = yuvCoefficients(matrix);
    const float kr = coefficients.kr;
    const float kb = coefficients.kb;
    const float kg = 1.0f - kr - kb;
    const float y = kr * r + kg * g + kb * b;
    const float cb = (b - y) / (2.0f * (1.0f - kb));
    const float cr = (r - y) / (2.0f * (1.0f - kr));

    using enum pek::YuvRange;

    if (range == Full) {
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

RgbBytes rgbFromColor(pek::Color color) noexcept {
    return {
        pek::Colors::getRed(color),
        pek::Colors::getGreen(color),
        pek::Colors::getBlue(color),
    };
}

TargetColor makeTargetColor(pek::Color color, const SurfaceLayout &layout) noexcept {
    return makeTargetColor(rgbFromColor(color), layout.yuvMatrix, layout.yuvRange);
}

std::uint8_t scaleAlpha(std::uint8_t value, std::uint8_t maxAlpha) noexcept {
    if (maxAlpha == 255U) {
        return value;
    }
    return static_cast<std::uint8_t>((static_cast<unsigned>(value) * maxAlpha + 127U) / 255U);
}

std::uint8_t blendByte(std::uint8_t dst, std::uint8_t src, std::uint8_t alpha) noexcept {
    if (alpha == 0U) {
        return dst;
    }
    if (alpha == 255U) {
        return src;
    }
    const unsigned invAlpha = 255U - alpha;
    return static_cast<std::uint8_t>(
        (static_cast<unsigned>(src) * alpha + static_cast<unsigned>(dst) * invAlpha + 127U) / 255U);
}

std::uint64_t scaleStep(std::size_t source, std::size_t destination) noexcept {
    if (source == 0 || destination == 0) {
        return 0;
    }

    constexpr std::size_t MaxScaledSource = std::numeric_limits<std::uint32_t>::max();
    const auto scaledSource = std::min(source, MaxScaledSource);
    return (scaledSource << 32U) / destination;
}

std::size_t scaledIndex(std::uint64_t accumulator, std::size_t source) noexcept {
    if (source == 0) {
        return 0;
    }
    const auto scaled = accumulator >> 32U;
    const auto maxIndex = source - 1U;
    return scaled < maxIndex ? scaled : maxIndex;
}

std::uint8_t maskBlockCoverageAlpha(const MaskView &mask,
                                    std::uint64_t xAccumulator,
                                    std::uint64_t yAccumulator,
                                    std::uint64_t xStep,
                                    std::uint64_t yStep,
                                    std::uint32_t width,
                                    std::uint32_t height,
                                    std::uint8_t threshold) noexcept {
    std::uint32_t covered = 0U;
    for (std::uint32_t y = 0; y < height; ++y) {
        const auto *maskRow =
            mask.data +
            scaledIndex(yAccumulator + static_cast<std::uint64_t>(y) * yStep, mask.height) *
                mask.width;
        for (std::uint32_t x = 0; x < width; ++x) {
            if (maskRow[scaledIndex(xAccumulator + static_cast<std::uint64_t>(x) * xStep,
                                    mask.width)] >= threshold) {
                ++covered;
            }
        }
    }
    const auto total = width * height;
    return total == 0U ? 0U : static_cast<std::uint8_t>((covered * 255U + total / 2U) / total);
}

bool validateMask(const MaskView &mask) noexcept {
    if (mask.data == nullptr || mask.width == 0 || mask.height == 0 ||
        multiplyOverflows(mask.width, mask.height)) {
        return false;
    }
    return mask.size >= mask.width * mask.height;
}

BitmapView makeBitmapView(const pek::Bitmap *bitmap) noexcept {
    if (bitmap == nullptr || bitmap->getType() != pek::Bitmap::Type::Uint32 ||
        bitmap->getWidth() == 0 || bitmap->getHeight() == 0 ||
        multiplyOverflows(bitmap->getWidth(), bitmap->getHeight()) ||
        bitmap->getWidth() * bitmap->getHeight() > std::numeric_limits<std::size_t>::max() / 4U ||
        bitmap->getPixels().size() < bitmap->getWidth() * bitmap->getHeight() * 4U) {
        return {};
    }

    return {
        bitmap->getData(),
        bitmap->getWidth(),
        bitmap->getHeight(),
    };
}

RgbBytes sampleBackgroundRgb(const BitmapView &bitmap,
                             std::uint64_t xAccumulator,
                             std::uint64_t yAccumulator) noexcept {
    const auto x = scaledIndex(xAccumulator, bitmap.width);
    const auto y = scaledIndex(yAccumulator, bitmap.height);
    const auto *pixel = bitmap.data + ((y * bitmap.width + x) * 4U);
    return {pixel[2], pixel[1], pixel[0]};
}

bool prepareLayout( // NOSONAR: explicit pixel-format validation mirrors memory layouts.
    const ImageSurfaceView &surface,
    SurfaceLayout &layout) noexcept {
    if (surface.width == 0 || surface.height == 0) {
        return false;
    }

    layout = SurfaceLayout{
        .format = surface.format,
        .width = surface.width,
        .height = surface.height,
        .planes = surface.planes,
        .yuvMatrix = resolveMatrix(surface.yuvMatrix, surface.height),
        .yuvRange = resolveRange(surface.yuvRange),
    };

    using enum pek::RawImagePixelFormat;

    const auto width = static_cast<std::size_t>(surface.width);
    const auto height = static_cast<std::size_t>(surface.height);
    switch (surface.format) {
    case Bgra:
        if (surface.planes.size() < 1U || width > std::numeric_limits<std::size_t>::max() / 4U ||
            !hasPlaneBytes(surface.planes[0], width * 4U, height)) {
            return false;
        }
        layout.plane0Stride =
            surface.planes[0].strideBytes != 0 ? surface.planes[0].strideBytes : width * 4U;
        return true;
    case Rgb:
        if (surface.planes.size() < 1U || width > std::numeric_limits<std::size_t>::max() / 3U ||
            !hasPlaneBytes(surface.planes[0], width * 3U, height)) {
            return false;
        }
        layout.plane0Stride =
            surface.planes[0].strideBytes != 0 ? surface.planes[0].strideBytes : width * 3U;
        return true;
    case I420: {
        const std::size_t chromaWidth = (width + 1U) / 2U;
        if (const std::size_t chromaHeight = (height + 1U) / 2U;
            surface.planes.size() < 3U || !hasPlaneBytes(surface.planes[0], width, height) ||
            !hasPlaneBytes(surface.planes[1], chromaWidth, chromaHeight) ||
            !hasPlaneBytes(surface.planes[2], chromaWidth, chromaHeight)) {
            return false;
        }
        layout.plane0Stride =
            surface.planes[0].strideBytes != 0 ? surface.planes[0].strideBytes : width;
        layout.plane1Stride =
            surface.planes[1].strideBytes != 0 ? surface.planes[1].strideBytes : chromaWidth;
        layout.plane2Stride =
            surface.planes[2].strideBytes != 0 ? surface.planes[2].strideBytes : chromaWidth;
        return true;
    }
    case Nv12: {
        const std::size_t chromaWidth = (width + 1U) / 2U;
        if (const std::size_t chromaHeight = (height + 1U) / 2U;
            surface.planes.size() < 2U || multiplyOverflows(chromaWidth, std::size_t{2}) ||
            !hasPlaneBytes(surface.planes[0], width, height) ||
            !hasPlaneBytes(surface.planes[1], chromaWidth * 2U, chromaHeight)) {
            return false;
        }
        layout.plane0Stride =
            surface.planes[0].strideBytes != 0 ? surface.planes[0].strideBytes : width;
        layout.plane1Stride =
            surface.planes[1].strideBytes != 0 ? surface.planes[1].strideBytes : chromaWidth * 2U;
        return true;
    }
    case Yuy2: {
        const std::size_t rowBytes = ((width + 1U) / 2U) * 4U;
        if (surface.planes.size() < 1U || width > std::numeric_limits<std::size_t>::max() - 1U ||
            !hasPlaneBytes(surface.planes[0], rowBytes, height)) {
            return false;
        }
        layout.plane0Stride =
            surface.planes[0].strideBytes != 0 ? surface.planes[0].strideBytes : rowBytes;
        return true;
    }
    default:
        return false;
    }
}

void blendBgra(const SurfaceLayout &layout,
               const MaskView &mask,
               const TargetColor &color,
               std::uint8_t maxAlpha) noexcept {
    const auto xStep = scaleStep(mask.width, layout.width);
    const auto yStep = scaleStep(mask.height, layout.height);
    std::uint64_t yAccumulator = 0;
    for (std::uint32_t y = 0; y < layout.height; ++y, yAccumulator += yStep) {
        const auto *maskRow = mask.data + scaledIndex(yAccumulator, mask.height) * mask.width;
        auto *row =
            layout.planes[0].mutableData + static_cast<std::size_t>(y) * layout.plane0Stride;
        std::uint64_t xAccumulator = 0;
        for (std::uint32_t x = 0; x < layout.width; ++x, xAccumulator += xStep) {
            const auto alpha = scaleAlpha(maskRow[scaledIndex(xAccumulator, mask.width)], maxAlpha);
            if (alpha == 0U) {
                continue;
            }

            auto *pixel = row + static_cast<std::size_t>(x) * 4U;
            pixel[0] = blendByte(pixel[0], color.b, alpha);
            pixel[1] = blendByte(pixel[1], color.g, alpha);
            pixel[2] = blendByte(pixel[2], color.r, alpha);
            pixel[3] = 255U;
        }
    }
}

void blendRgb(const SurfaceLayout &layout,
              const MaskView &mask,
              const TargetColor &color,
              std::uint8_t maxAlpha) noexcept {
    const auto xStep = scaleStep(mask.width, layout.width);
    const auto yStep = scaleStep(mask.height, layout.height);
    std::uint64_t yAccumulator = 0;
    for (std::uint32_t y = 0; y < layout.height; ++y, yAccumulator += yStep) {
        const auto *maskRow = mask.data + scaledIndex(yAccumulator, mask.height) * mask.width;
        auto *row =
            layout.planes[0].mutableData + static_cast<std::size_t>(y) * layout.plane0Stride;
        std::uint64_t xAccumulator = 0;
        for (std::uint32_t x = 0; x < layout.width; ++x, xAccumulator += xStep) {
            const auto alpha = scaleAlpha(maskRow[scaledIndex(xAccumulator, mask.width)], maxAlpha);
            if (alpha == 0U) {
                continue;
            }

            auto *pixel = row + static_cast<std::size_t>(x) * 3U;
            pixel[0] = blendByte(pixel[0], color.r, alpha);
            pixel[1] = blendByte(pixel[1], color.g, alpha);
            pixel[2] = blendByte(pixel[2], color.b, alpha);
        }
    }
}

void blendI420(const SurfaceLayout &layout,
               const MaskView &mask,
               const TargetColor &color,
               std::uint8_t maxAlpha) noexcept {
    const auto xStep = scaleStep(mask.width, layout.width);
    const auto yStep = scaleStep(mask.height, layout.height);
    std::uint64_t yAccumulator = 0;
    for (std::uint32_t y = 0; y < layout.height; ++y, yAccumulator += yStep) {
        const auto *maskRow = mask.data + scaledIndex(yAccumulator, mask.height) * mask.width;
        auto *row =
            layout.planes[0].mutableData + static_cast<std::size_t>(y) * layout.plane0Stride;
        std::uint64_t xAccumulator = 0;
        for (std::uint32_t x = 0; x < layout.width; ++x, xAccumulator += xStep) {
            const auto alpha = scaleAlpha(maskRow[scaledIndex(xAccumulator, mask.width)], maxAlpha);
            if (alpha != 0U) {
                row[x] = blendByte(row[x], color.y, alpha);
            }
        }
    }

    const auto chromaWidth = (static_cast<std::size_t>(layout.width) + 1U) / 2U;
    const auto chromaHeight = (static_cast<std::size_t>(layout.height) + 1U) / 2U;
    const auto cxStep = scaleStep(mask.width, chromaWidth);
    const auto cyStep = scaleStep(mask.height, chromaHeight);
    std::uint64_t cyAccumulator = 0;
    for (std::size_t cy = 0; cy < chromaHeight; ++cy, cyAccumulator += cyStep) {
        const auto *maskRow = mask.data + scaledIndex(cyAccumulator, mask.height) * mask.width;
        auto *uRow = layout.planes[1].mutableData + cy * layout.plane1Stride;
        auto *vRow = layout.planes[2].mutableData + cy * layout.plane2Stride;
        std::uint64_t cxAccumulator = 0;
        for (std::size_t cx = 0; cx < chromaWidth; ++cx, cxAccumulator += cxStep) {
            const auto alpha =
                scaleAlpha(maskRow[scaledIndex(cxAccumulator, mask.width)], maxAlpha);
            if (alpha == 0U) {
                continue;
            }
            uRow[cx] = blendByte(uRow[cx], color.u, alpha);
            vRow[cx] = blendByte(vRow[cx], color.v, alpha);
        }
    }
}

void blendNv12(const SurfaceLayout &layout,
               const MaskView &mask,
               const TargetColor &color,
               std::uint8_t maxAlpha) noexcept {
    const auto xStep = scaleStep(mask.width, layout.width);
    const auto yStep = scaleStep(mask.height, layout.height);
    std::uint64_t yAccumulator = 0;
    for (std::uint32_t y = 0; y < layout.height; ++y, yAccumulator += yStep) {
        const auto *maskRow = mask.data + scaledIndex(yAccumulator, mask.height) * mask.width;
        auto *row =
            layout.planes[0].mutableData + static_cast<std::size_t>(y) * layout.plane0Stride;
        std::uint64_t xAccumulator = 0;
        for (std::uint32_t x = 0; x < layout.width; ++x, xAccumulator += xStep) {
            const auto alpha = scaleAlpha(maskRow[scaledIndex(xAccumulator, mask.width)], maxAlpha);
            if (alpha != 0U) {
                row[x] = blendByte(row[x], color.y, alpha);
            }
        }
    }

    const auto chromaWidth = (static_cast<std::size_t>(layout.width) + 1U) / 2U;
    const auto chromaHeight = (static_cast<std::size_t>(layout.height) + 1U) / 2U;
    const auto cxStep = scaleStep(mask.width, chromaWidth);
    const auto cyStep = scaleStep(mask.height, chromaHeight);
    std::uint64_t cyAccumulator = 0;
    for (std::size_t cy = 0; cy < chromaHeight; ++cy, cyAccumulator += cyStep) {
        const auto *maskRow = mask.data + scaledIndex(cyAccumulator, mask.height) * mask.width;
        auto *uvRow = layout.planes[1].mutableData + cy * layout.plane1Stride;
        std::uint64_t cxAccumulator = 0;
        for (std::size_t cx = 0; cx < chromaWidth; ++cx, cxAccumulator += cxStep) {
            const auto alpha =
                scaleAlpha(maskRow[scaledIndex(cxAccumulator, mask.width)], maxAlpha);
            if (alpha == 0U) {
                continue;
            }
            auto *uv = uvRow + cx * 2U;
            uv[0] = blendByte(uv[0], color.u, alpha);
            uv[1] = blendByte(uv[1], color.v, alpha);
        }
    }
}

void blendYuy2(const SurfaceLayout &layout,
               const MaskView &mask,
               const TargetColor &color,
               std::uint8_t maxAlpha) noexcept {
    const auto xStep = scaleStep(mask.width, layout.width);
    const auto yStep = scaleStep(mask.height, layout.height);
    const auto pairWidth = (static_cast<std::size_t>(layout.width) + 1U) / 2U;
    const auto pairXStep = scaleStep(mask.width, pairWidth);

    std::uint64_t yAccumulator = 0;
    for (std::uint32_t y = 0; y < layout.height; ++y, yAccumulator += yStep) {
        const auto *maskRow = mask.data + scaledIndex(yAccumulator, mask.height) * mask.width;
        auto *row =
            layout.planes[0].mutableData + static_cast<std::size_t>(y) * layout.plane0Stride;
        std::uint64_t xAccumulator = 0;
        for (std::uint32_t x = 0; x < layout.width; ++x, xAccumulator += xStep) {
            const auto alpha = scaleAlpha(maskRow[scaledIndex(xAccumulator, mask.width)], maxAlpha);
            if (alpha == 0U) {
                continue;
            }

            auto *pair = row + (static_cast<std::size_t>(x) / 2U) * 4U;
            auto &luma = (x % 2U == 0U) ? pair[0] : pair[2];
            luma = blendByte(luma, color.y, alpha);
        }

        std::uint64_t pairAccumulator = 0;
        for (std::size_t pairIndex = 0; pairIndex < pairWidth;
             ++pairIndex, pairAccumulator += pairXStep) {
            const auto alpha =
                scaleAlpha(maskRow[scaledIndex(pairAccumulator, mask.width)], maxAlpha);
            if (alpha == 0U) {
                continue;
            }

            auto *pair = row + pairIndex * 4U;
            pair[1] = blendByte(pair[1], color.u, alpha);
            pair[3] = blendByte(pair[3], color.v, alpha);
        }
    }
}

void replaceBgra(const SurfaceLayout &layout,
                 const MaskView &mask,
                 const TargetColor &fallback,
                 const BitmapView &background,
                 std::uint8_t threshold) noexcept {
    const bool hasBackground = background.data != nullptr;
    const auto maskXStep = scaleStep(mask.width, layout.width);
    const auto maskYStep = scaleStep(mask.height, layout.height);
    const auto bgXStep = scaleStep(background.width, layout.width);
    const auto bgYStep = scaleStep(background.height, layout.height);
    std::uint64_t maskYAccumulator = 0;
    std::uint64_t bgYAccumulator = 0;
    for (std::uint32_t y = 0; y < layout.height;
         ++y, maskYAccumulator += maskYStep, bgYAccumulator += bgYStep) {
        const auto *maskRow = mask.data + scaledIndex(maskYAccumulator, mask.height) * mask.width;
        auto *row =
            layout.planes[0].mutableData + static_cast<std::size_t>(y) * layout.plane0Stride;
        std::uint64_t maskXAccumulator = 0;
        std::uint64_t bgXAccumulator = 0;
        for (std::uint32_t x = 0; x < layout.width;
             ++x, maskXAccumulator += maskXStep, bgXAccumulator += bgXStep) {
            if (maskRow[scaledIndex(maskXAccumulator, mask.width)] < threshold) {
                continue;
            }

            auto *pixel = row + static_cast<std::size_t>(x) * 4U;
            if (hasBackground) {
                const auto bg = sampleBackgroundRgb(background, bgXAccumulator, bgYAccumulator);
                pixel[0] = bg.b;
                pixel[1] = bg.g;
                pixel[2] = bg.r;
            } else {
                pixel[0] = fallback.b;
                pixel[1] = fallback.g;
                pixel[2] = fallback.r;
            }
            pixel[3] = 255U;
        }
    }
}

void replaceRgb(const SurfaceLayout &layout,
                const MaskView &mask,
                const TargetColor &fallback,
                const BitmapView &background,
                std::uint8_t threshold) noexcept {
    const bool hasBackground = background.data != nullptr;
    const auto maskXStep = scaleStep(mask.width, layout.width);
    const auto maskYStep = scaleStep(mask.height, layout.height);
    const auto bgXStep = scaleStep(background.width, layout.width);
    const auto bgYStep = scaleStep(background.height, layout.height);
    std::uint64_t maskYAccumulator = 0;
    std::uint64_t bgYAccumulator = 0;
    for (std::uint32_t y = 0; y < layout.height;
         ++y, maskYAccumulator += maskYStep, bgYAccumulator += bgYStep) {
        const auto *maskRow = mask.data + scaledIndex(maskYAccumulator, mask.height) * mask.width;
        auto *row =
            layout.planes[0].mutableData + static_cast<std::size_t>(y) * layout.plane0Stride;
        std::uint64_t maskXAccumulator = 0;
        std::uint64_t bgXAccumulator = 0;
        for (std::uint32_t x = 0; x < layout.width;
             ++x, maskXAccumulator += maskXStep, bgXAccumulator += bgXStep) {
            if (maskRow[scaledIndex(maskXAccumulator, mask.width)] < threshold) {
                continue;
            }

            auto *pixel = row + static_cast<std::size_t>(x) * 3U;
            if (hasBackground) {
                const auto bg = sampleBackgroundRgb(background, bgXAccumulator, bgYAccumulator);
                pixel[0] = bg.r;
                pixel[1] = bg.g;
                pixel[2] = bg.b;
            } else {
                pixel[0] = fallback.r;
                pixel[1] = fallback.g;
                pixel[2] = fallback.b;
            }
        }
    }
}

TargetColor replacementColorAt(const SurfaceLayout &layout,
                               const TargetColor &fallback,
                               const BitmapView &background,
                               std::uint64_t bgXAccumulator,
                               std::uint64_t bgYAccumulator) noexcept {
    if (background.data == nullptr) {
        return fallback;
    }
    return makeTargetColor(sampleBackgroundRgb(background, bgXAccumulator, bgYAccumulator),
                           layout.yuvMatrix,
                           layout.yuvRange);
}

void replaceI420(const SurfaceLayout &layout,
                 const MaskView &mask,
                 const TargetColor &fallback,
                 const BitmapView &background,
                 std::uint8_t threshold) noexcept {
    const auto maskXStep = scaleStep(mask.width, layout.width);
    const auto maskYStep = scaleStep(mask.height, layout.height);
    const auto bgXStep = scaleStep(background.width, layout.width);
    const auto bgYStep = scaleStep(background.height, layout.height);
    std::uint64_t maskYAccumulator = 0;
    std::uint64_t bgYAccumulator = 0;
    for (std::uint32_t y = 0; y < layout.height;
         ++y, maskYAccumulator += maskYStep, bgYAccumulator += bgYStep) {
        const auto *maskRow = mask.data + scaledIndex(maskYAccumulator, mask.height) * mask.width;
        auto *row =
            layout.planes[0].mutableData + static_cast<std::size_t>(y) * layout.plane0Stride;
        std::uint64_t maskXAccumulator = 0;
        std::uint64_t bgXAccumulator = 0;
        for (std::uint32_t x = 0; x < layout.width;
             ++x, maskXAccumulator += maskXStep, bgXAccumulator += bgXStep) {
            if (maskRow[scaledIndex(maskXAccumulator, mask.width)] < threshold) {
                continue;
            }

            row[x] =
                replacementColorAt(layout, fallback, background, bgXAccumulator, bgYAccumulator).y;
        }
    }

    const auto chromaWidth = (static_cast<std::size_t>(layout.width) + 1U) / 2U;
    const auto chromaHeight = (static_cast<std::size_t>(layout.height) + 1U) / 2U;
    const auto bgCxStep = scaleStep(background.width, chromaWidth);
    const auto bgCyStep = scaleStep(background.height, chromaHeight);
    std::uint64_t bgCyAccumulator = 0;
    for (std::size_t cy = 0; cy < chromaHeight; ++cy, bgCyAccumulator += bgCyStep) {
        auto *uRow = layout.planes[1].mutableData + cy * layout.plane1Stride;
        auto *vRow = layout.planes[2].mutableData + cy * layout.plane2Stride;
        std::uint64_t bgCxAccumulator = 0;
        for (std::size_t cx = 0; cx < chromaWidth; ++cx, bgCxAccumulator += bgCxStep) {
            const auto x = static_cast<std::uint32_t>(cx * 2U);
            const auto y = static_cast<std::uint32_t>(cy * 2U);
            const auto blockWidth = std::min<std::uint32_t>(2U, layout.width - x);
            const auto blockHeight = std::min<std::uint32_t>(2U, layout.height - y);
            const auto alpha = maskBlockCoverageAlpha(mask,
                                                      static_cast<std::uint64_t>(x) * maskXStep,
                                                      static_cast<std::uint64_t>(y) * maskYStep,
                                                      maskXStep,
                                                      maskYStep,
                                                      blockWidth,
                                                      blockHeight,
                                                      threshold);
            if (alpha == 0U) {
                continue;
            }

            const auto color =
                replacementColorAt(layout, fallback, background, bgCxAccumulator, bgCyAccumulator);
            uRow[cx] = blendByte(uRow[cx], color.u, alpha);
            vRow[cx] = blendByte(vRow[cx], color.v, alpha);
        }
    }
}

void replaceNv12(const SurfaceLayout &layout,
                 const MaskView &mask,
                 const TargetColor &fallback,
                 const BitmapView &background,
                 std::uint8_t threshold) noexcept {
    const auto maskXStep = scaleStep(mask.width, layout.width);
    const auto maskYStep = scaleStep(mask.height, layout.height);
    const auto bgXStep = scaleStep(background.width, layout.width);
    const auto bgYStep = scaleStep(background.height, layout.height);
    std::uint64_t maskYAccumulator = 0;
    std::uint64_t bgYAccumulator = 0;
    for (std::uint32_t y = 0; y < layout.height;
         ++y, maskYAccumulator += maskYStep, bgYAccumulator += bgYStep) {
        const auto *maskRow = mask.data + scaledIndex(maskYAccumulator, mask.height) * mask.width;
        auto *row =
            layout.planes[0].mutableData + static_cast<std::size_t>(y) * layout.plane0Stride;
        std::uint64_t maskXAccumulator = 0;
        std::uint64_t bgXAccumulator = 0;
        for (std::uint32_t x = 0; x < layout.width;
             ++x, maskXAccumulator += maskXStep, bgXAccumulator += bgXStep) {
            if (maskRow[scaledIndex(maskXAccumulator, mask.width)] < threshold) {
                continue;
            }

            row[x] =
                replacementColorAt(layout, fallback, background, bgXAccumulator, bgYAccumulator).y;
        }
    }

    const auto chromaWidth = (static_cast<std::size_t>(layout.width) + 1U) / 2U;
    const auto chromaHeight = (static_cast<std::size_t>(layout.height) + 1U) / 2U;
    const auto bgCxStep = scaleStep(background.width, chromaWidth);
    const auto bgCyStep = scaleStep(background.height, chromaHeight);
    std::uint64_t bgCyAccumulator = 0;
    for (std::size_t cy = 0; cy < chromaHeight; ++cy, bgCyAccumulator += bgCyStep) {
        auto *uvRow = layout.planes[1].mutableData + cy * layout.plane1Stride;
        std::uint64_t bgCxAccumulator = 0;
        for (std::size_t cx = 0; cx < chromaWidth; ++cx, bgCxAccumulator += bgCxStep) {
            const auto x = static_cast<std::uint32_t>(cx * 2U);
            const auto y = static_cast<std::uint32_t>(cy * 2U);
            const auto blockWidth = std::min<std::uint32_t>(2U, layout.width - x);
            const auto blockHeight = std::min<std::uint32_t>(2U, layout.height - y);
            const auto alpha = maskBlockCoverageAlpha(mask,
                                                      static_cast<std::uint64_t>(x) * maskXStep,
                                                      static_cast<std::uint64_t>(y) * maskYStep,
                                                      maskXStep,
                                                      maskYStep,
                                                      blockWidth,
                                                      blockHeight,
                                                      threshold);
            if (alpha == 0U) {
                continue;
            }

            const auto color =
                replacementColorAt(layout, fallback, background, bgCxAccumulator, bgCyAccumulator);
            auto *uv = uvRow + cx * 2U;
            uv[0] = blendByte(uv[0], color.u, alpha);
            uv[1] = blendByte(uv[1], color.v, alpha);
        }
    }
}

void replaceYuy2(const SurfaceLayout &layout,
                 const MaskView &mask,
                 const TargetColor &fallback,
                 const BitmapView &background,
                 std::uint8_t threshold) noexcept {
    const auto maskXStep = scaleStep(mask.width, layout.width);
    const auto maskYStep = scaleStep(mask.height, layout.height);
    const auto bgXStep = scaleStep(background.width, layout.width);
    const auto bgYStep = scaleStep(background.height, layout.height);
    const auto pairWidth = (static_cast<std::size_t>(layout.width) + 1U) / 2U;
    const auto bgPairXStep = scaleStep(background.width, pairWidth);

    std::uint64_t maskYAccumulator = 0;
    std::uint64_t bgYAccumulator = 0;
    for (std::uint32_t y = 0; y < layout.height;
         ++y, maskYAccumulator += maskYStep, bgYAccumulator += bgYStep) {
        const auto *maskRow = mask.data + scaledIndex(maskYAccumulator, mask.height) * mask.width;
        auto *row =
            layout.planes[0].mutableData + static_cast<std::size_t>(y) * layout.plane0Stride;
        std::uint64_t maskXAccumulator = 0;
        std::uint64_t bgXAccumulator = 0;
        for (std::uint32_t x = 0; x < layout.width;
             ++x, maskXAccumulator += maskXStep, bgXAccumulator += bgXStep) {
            if (maskRow[scaledIndex(maskXAccumulator, mask.width)] < threshold) {
                continue;
            }

            auto *pair = row + (static_cast<std::size_t>(x) / 2U) * 4U;
            auto &luma = (x % 2U == 0U) ? pair[0] : pair[2];
            luma =
                replacementColorAt(layout, fallback, background, bgXAccumulator, bgYAccumulator).y;
        }

        std::uint64_t bgPairXAccumulator = 0;
        for (std::size_t pairIndex = 0; pairIndex < pairWidth;
             ++pairIndex, bgPairXAccumulator += bgPairXStep) {
            const auto x = static_cast<std::uint32_t>(pairIndex * 2U);
            const auto blockWidth = std::min<std::uint32_t>(2U, layout.width - x);
            const auto alpha = maskBlockCoverageAlpha(mask,
                                                      static_cast<std::uint64_t>(x) * maskXStep,
                                                      maskYAccumulator,
                                                      maskXStep,
                                                      maskYStep,
                                                      blockWidth,
                                                      1U,
                                                      threshold);
            if (alpha == 0U) {
                continue;
            }

            const auto color = replacementColorAt(
                layout, fallback, background, bgPairXAccumulator, bgYAccumulator);
            auto *pair = row + pairIndex * 4U;
            pair[1] = blendByte(pair[1], color.u, alpha);
            pair[3] = blendByte(pair[3], color.v, alpha);
        }
    }
}

} // namespace

bool blendSegmentationMask(const ImageSurfaceView &surface,
                           const MaskView &mask,
                           const SegmentationMaskOptions &options) noexcept {
    SurfaceLayout layout;
    if (!validateMask(mask) || !prepareLayout(surface, layout)) {
        return false;
    }

    const auto color = makeTargetColor(options.color, layout);
    using enum pek::RawImagePixelFormat;

    switch (layout.format) {
    case Bgra:
        blendBgra(layout, mask, color, options.maxAlpha);
        return true;
    case Rgb:
        blendRgb(layout, mask, color, options.maxAlpha);
        return true;
    case I420:
        blendI420(layout, mask, color, options.maxAlpha);
        return true;
    case Nv12:
        blendNv12(layout, mask, color, options.maxAlpha);
        return true;
    case Yuy2:
        blendYuy2(layout, mask, color, options.maxAlpha);
        return true;
    default:
        return false;
    }
}

bool replaceBackgroundFromMask(const ImageSurfaceView &surface,
                               const MaskView &mask,
                               const BackgroundReplacementOptions &options) noexcept {
    SurfaceLayout layout;
    if (!validateMask(mask) || !prepareLayout(surface, layout)) {
        return false;
    }

    const auto fallback = makeTargetColor(options.fallbackColor, layout);
    const auto background = makeBitmapView(options.backgroundImage);
    using enum pek::RawImagePixelFormat;

    switch (layout.format) {
    case Bgra:
        replaceBgra(layout, mask, fallback, background, options.threshold);
        return true;
    case Rgb:
        replaceRgb(layout, mask, fallback, background, options.threshold);
        return true;
    case I420:
        replaceI420(layout, mask, fallback, background, options.threshold);
        return true;
    case Nv12:
        replaceNv12(layout, mask, fallback, background, options.threshold);
        return true;
    case Yuy2:
        replaceYuy2(layout, mask, fallback, background, options.threshold);
        return true;
    default:
        return false;
    }
}

} // namespace pek::raster
