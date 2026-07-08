/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#include "preproc/CpuImageKernels.h"
#include "pek/Types.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

using namespace pek::stdop::preproc;
using pek::Float16;

namespace {
struct Rgbf {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

inline bool canRunDirectFullKernel(const pek::ImageOpDesc &src, const pek::ImageOpDesc &dst) {
    return !dst.keepAspectRatio && src.rectIsFullSurface() && dst.rectIsFullSurface() &&
           src.surfaceWidth == dst.surfaceWidth && src.surfaceHeight == dst.surfaceHeight;
}

Rgbf letterboxRgb(const pek::ImageOpDesc &dst) {
    return Rgbf{
        std::clamp(dst.letterboxRed, 0.0f, 1.0f),
        std::clamp(dst.letterboxGreen, 0.0f, 1.0f),
        std::clamp(dst.letterboxBlue, 0.0f, 1.0f),
    };
}

Rgbf applyMeanStd(const Rgbf &rgb, const pek::Colorf &mean, const pek::Colorf &std) {
    constexpr float eps = 1e-12f;
    return Rgbf{
        (rgb.r - mean.r) / std::max(std.r, eps),
        (rgb.g - mean.g) / std::max(std.g, eps),
        (rgb.b - mean.b) / std::max(std.b, eps),
    };
}

uint8_t toByte(float value) {
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

uint8_t rgbToGrayByte(const Rgbf &rgb) {
    const auto r = static_cast<uint16_t>(toByte(rgb.r));
    const auto g = static_cast<uint16_t>(toByte(rgb.g));
    const auto b = static_cast<uint16_t>(toByte(rgb.b));
    return static_cast<uint8_t>((77u * r + 150u * g + 29u * b + 128u) >> 8);
}

float rgbToGrayFloat(const Rgbf &rgb) {
    return 0.299f * rgb.r + 0.587f * rgb.g + 0.114f * rgb.b;
}

bool makeLetterboxDestination(const pek::ImageOpDesc &src,
                              const pek::ImageOpDesc &dst,
                              pek::ImageOpDesc &innerDst) {
    innerDst = dst;
    innerDst.rect = pek::computeLetterboxInnerRect(src.rect, dst.rect);
    innerDst.keepAspectRatio = false;
    return !innerDst.rect.isEmpty();
}

void fillRgbF32Chw(const pek::ImageOpDesc &dst, const pek::Colorf &mean, const pek::Colorf &std) {
    auto *out = reinterpret_cast<float *>(dst.data);
    const size_t planeSize = dst.surfaceWidth * dst.surfaceHeight;
    auto rgb = letterboxRgb(dst);
    if (!pek::MeanStd::isDefaultMean(mean) || !pek::MeanStd::isDefaultStd(std)) {
        rgb = applyMeanStd(rgb, mean, std);
    }

    for (size_t y = 0; y < dst.rect.height; ++y) {
        const size_t dyi = dst.rect.y + y;
        for (size_t x = 0; x < dst.rect.width; ++x) {
            const size_t dxi = dst.rect.x + x;
            const size_t hw = dyi * dst.surfaceWidth + dxi;
            out[0 * planeSize + hw] = rgb.r;
            out[1 * planeSize + hw] = rgb.g;
            out[2 * planeSize + hw] = rgb.b;
        }
    }
}

void fillRgbF16Chw(const pek::ImageOpDesc &dst, const pek::Colorf &mean, const pek::Colorf &std) {
    auto *out = reinterpret_cast<Float16 *>(dst.data);
    const size_t planeSize = dst.surfaceWidth * dst.surfaceHeight;
    auto rgb = letterboxRgb(dst);
    if (!pek::MeanStd::isDefaultMean(mean) || !pek::MeanStd::isDefaultStd(std)) {
        rgb = applyMeanStd(rgb, mean, std);
    }

    for (size_t y = 0; y < dst.rect.height; ++y) {
        const size_t dyi = dst.rect.y + y;
        for (size_t x = 0; x < dst.rect.width; ++x) {
            const size_t dxi = dst.rect.x + x;
            const size_t hw = dyi * dst.surfaceWidth + dxi;
            out[0 * planeSize + hw] = static_cast<Float16>(rgb.r);
            out[1 * planeSize + hw] = static_cast<Float16>(rgb.g);
            out[2 * planeSize + hw] = static_cast<Float16>(rgb.b);
        }
    }
}

void fillRgb8Hwc(const pek::ImageOpDesc &dst) {
    auto *out = static_cast<uint8_t *>(dst.data);
    const auto rgb = letterboxRgb(dst);
    const uint8_t r = toByte(rgb.r);
    const uint8_t g = toByte(rgb.g);
    const uint8_t b = toByte(rgb.b);

    for (size_t y = 0; y < dst.rect.height; ++y) {
        const size_t dyi = dst.rect.y + y;
        for (size_t x = 0; x < dst.rect.width; ++x) {
            const size_t dxi = dst.rect.x + x;
            const size_t index = (dyi * dst.surfaceWidth + dxi) * 3;
            out[index + 0] = r;
            out[index + 1] = g;
            out[index + 2] = b;
        }
    }
}

void fillRgbF32Hwc(const pek::ImageOpDesc &dst, const pek::Colorf &mean, const pek::Colorf &std) {
    auto *out = reinterpret_cast<float *>(dst.data);
    auto rgb = letterboxRgb(dst);
    if (!pek::MeanStd::isDefaultMean(mean) || !pek::MeanStd::isDefaultStd(std)) {
        rgb = applyMeanStd(rgb, mean, std);
    }

    for (size_t y = 0; y < dst.rect.height; ++y) {
        const size_t dyi = dst.rect.y + y;
        for (size_t x = 0; x < dst.rect.width; ++x) {
            const size_t dxi = dst.rect.x + x;
            auto *pixel = out + (dyi * dst.surfaceWidth + dxi) * 3;
            pixel[0] = rgb.r;
            pixel[1] = rgb.g;
            pixel[2] = rgb.b;
        }
    }
}

void fillRgbF16Hwc(const pek::ImageOpDesc &dst, const pek::Colorf &mean, const pek::Colorf &std) {
    auto *out = reinterpret_cast<Float16 *>(dst.data);
    auto rgb = letterboxRgb(dst);
    if (!pek::MeanStd::isDefaultMean(mean) || !pek::MeanStd::isDefaultStd(std)) {
        rgb = applyMeanStd(rgb, mean, std);
    }

    for (size_t y = 0; y < dst.rect.height; ++y) {
        const size_t dyi = dst.rect.y + y;
        for (size_t x = 0; x < dst.rect.width; ++x) {
            const size_t dxi = dst.rect.x + x;
            auto *pixel = out + (dyi * dst.surfaceWidth + dxi) * 3;
            pixel[0] = static_cast<Float16>(rgb.r);
            pixel[1] = static_cast<Float16>(rgb.g);
            pixel[2] = static_cast<Float16>(rgb.b);
        }
    }
}

void fillGray8(const pek::ImageOpDesc &dst) {
    auto *out = static_cast<uint8_t *>(dst.data);
    const uint8_t gray = rgbToGrayByte(letterboxRgb(dst));

    for (size_t y = 0; y < dst.rect.height; ++y) {
        const size_t dyi = dst.rect.y + y;
        for (size_t x = 0; x < dst.rect.width; ++x) {
            const size_t dxi = dst.rect.x + x;
            out[dyi * dst.surfaceWidth + dxi] = gray;
        }
    }
}

void fillGrayF32(const pek::ImageOpDesc &dst, const pek::Colorf &mean, const pek::Colorf &std) {
    auto *out = reinterpret_cast<float *>(dst.data);
    float gray = rgbToGrayFloat(letterboxRgb(dst));
    if (!pek::MeanStd::isDefaultMean(mean) || !pek::MeanStd::isDefaultStd(std)) {
        constexpr float eps = 1e-12f;
        gray = (gray - mean.r) / std::max(std.r, eps);
    }

    for (size_t y = 0; y < dst.rect.height; ++y) {
        const size_t dyi = dst.rect.y + y;
        for (size_t x = 0; x < dst.rect.width; ++x) {
            const size_t dxi = dst.rect.x + x;
            out[dyi * dst.surfaceWidth + dxi] = gray;
        }
    }
}

const pek::Colorf defaultMean{0.0f, 0.0f, 0.0f, 0.0f};
const pek::Colorf defaultStd{1.0f, 1.0f, 1.0f, 1.0f};
} // namespace

// this one is called
bool ImageOps::StretchBlit_Bgra8_Hwc_Full_Rgbf32_Full_Chw(const ImageOpDesc &src,
                                                          const ImageOpDesc &dst,
                                                          Sampling sampling) {
    if (!canRunDirectFullKernel(src, dst)) {
        return StretchBlit_Bgra8_Hwc_Rect_Rgbf32_Rect_Chw(src, dst, sampling);
    }

    (void)sampling;

    const uint8_t *srcPtr = src.data;
    const size_t srcWidth = src.surfaceWidth;
    const size_t srcHeight = src.surfaceHeight;

    float *dstPtr = (float *)dst.data;
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;

    const Colorf &mean = src.mean;
    const Colorf &std = src.std;

    if (!srcPtr || !dstPtr)
        return false;

    constexpr float inv255 = 1.0f / 255.0f;
    const size_t planeSize = dstWidth * dstHeight;

    if (pek::MeanStd::isDefaultMean(mean) && pek::MeanStd::isDefaultStd(std)) {
        for (size_t y = 0; y < dstHeight; ++y) {
            for (size_t x = 0; x < dstWidth; ++x) {
                const uint8_t *p = srcPtr + (y * srcWidth + x) * 4;
                const size_t hw = y * dstWidth + x;
                dstPtr[0 * planeSize + hw] = p[2] * inv255;
                dstPtr[1 * planeSize + hw] = p[1] * inv255;
                dstPtr[2 * planeSize + hw] = p[0] * inv255;
            }
        }
    } else {
        const auto [meanR, meanG, meanB, meanA] = mean;
        const auto [stdR, stdG, stdB, stdA] = std;
        (void)meanA;
        (void)stdA;

        constexpr float eps = 1e-12f;
        const float invStdR = 1.0f / std::max(stdR, eps);
        const float invStdG = 1.0f / std::max(stdG, eps);
        const float invStdB = 1.0f / std::max(stdB, eps);

        for (size_t y = 0; y < dstHeight; ++y) {
            for (size_t x = 0; x < dstWidth; ++x) {
                const uint8_t *p = srcPtr + (y * srcWidth + x) * 4;
                const size_t hw = y * dstWidth + x;

                const float r01 = p[2] * inv255;
                const float g01 = p[1] * inv255;
                const float b01 = p[0] * inv255;

                dstPtr[0 * planeSize + hw] = (r01 - meanR) * invStdR;
                dstPtr[1 * planeSize + hw] = (g01 - meanG) * invStdG;
                dstPtr[2 * planeSize + hw] = (b01 - meanB) * invStdB;
            }
        }
    }

    return true;
}

bool ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgbf32_Rect_Chw(const ImageOpDesc &src,
                                                          const ImageOpDesc &dst,
                                                          Sampling sampling) {
    const uint8_t *srcPtr = src.data;
    size_t srcWidth = src.surfaceWidth;
    size_t srcHeight = src.surfaceHeight;
    const PixelRect &srcRect = src.rect;

    float *dstPtr = (float *)dst.data;
    size_t dstWidth = dst.surfaceWidth;
    size_t dstHeight = dst.surfaceHeight;
    const PixelRect &dstRect = dst.rect;

    const Colorf &mean = src.mean;
    const Colorf &std = src.std;

    if (!srcPtr || !dstPtr)
        return false;

    if (srcRect.x + srcRect.width > srcWidth || srcRect.y + srcRect.height > srcHeight ||
        dstRect.x + dstRect.width > dstWidth || dstRect.y + dstRect.height > dstHeight)
        return false;

    if (dst.keepAspectRatio) {
        pek::ImageOpDesc innerDst;
        if (!makeLetterboxDestination(src, dst, innerDst)) {
            return false;
        }
        fillRgbF32Chw(dst, mean, std);
        return StretchBlit_Bgra8_Hwc_Rect_Rgbf32_Rect_Chw(src, innerDst, sampling);
    }

    const uint8_t *in = srcPtr;
    float *out = dstPtr;
    constexpr float inv255 = 1.0f / 255.0f;

    const size_t C = 3;
    const size_t H = dstHeight;
    const size_t W = dstWidth;

    const size_t planeSize = H * W;

    // nearest-neighbour sampling stretch from srcRect to dstRect, CHW layout

    if (pek::MeanStd::isDefaultMean(mean) && pek::MeanStd::isDefaultStd(std)) {
        // default mean/std
        for (size_t dy = 0; dy < dstRect.height; ++dy) {
            const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
            const size_t dyi = dstRect.y + dy;

            for (size_t dx = 0; dx < dstRect.width; ++dx) {
                const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
                const size_t dxi = dstRect.x + dx;

                // source: BGRA interleaved, HWC
                const size_t srcIndexRGB = (sy * srcWidth + sx) * 4;
                const uint8_t *p = in + srcIndexRGB;

                // CHW destination
                const size_t hwIndex = dyi * W + dxi;

                out[0 * planeSize + hwIndex] = p[2] * inv255; // R channel
                out[1 * planeSize + hwIndex] = p[1] * inv255; // G channel
                out[2 * planeSize + hwIndex] = p[0] * inv255; // B channel
            }
        }
    } else {
        // non-default mean/std: (x - mean) / std, where x is in [0,1]
        const auto [meanR, meanG, meanB, meanA] = mean;
        const auto [stdR, stdG, stdB, stdA] = std;
        (void)meanA;
        (void)stdA;

        // Guard against division by zero (or near-zero) std
        constexpr float eps = 1e-12f;
        const float invStdR = 1.0f / std::max(stdR, eps);
        const float invStdG = 1.0f / std::max(stdG, eps);
        const float invStdB = 1.0f / std::max(stdB, eps);

        for (size_t dy = 0; dy < dstRect.height; ++dy) {
            const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
            const size_t dyi = dstRect.y + dy;

            for (size_t dx = 0; dx < dstRect.width; ++dx) {
                const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
                const size_t dxi = dstRect.x + dx;

                const size_t srcIndex = (sy * srcWidth + sx) * 4;
                const uint8_t *p = in + srcIndex;

                const size_t hwIndex = dyi * W + dxi;

                const float r01 = p[2] * inv255; // BGRA -> R
                const float g01 = p[1] * inv255; //        G
                const float b01 = p[0] * inv255; //        B

                out[0 * planeSize + hwIndex] = (r01 - meanR) * invStdR;
                out[1 * planeSize + hwIndex] = (g01 - meanG) * invStdG;
                out[2 * planeSize + hwIndex] = (b01 - meanB) * invStdB;
            }
        }
    }

    return true;
}

bool ImageOps::StretchBlit_Bgra8_Hwc_Full_Rgb8_Full_Hwc(const ImageOpDesc &src,
                                                        const ImageOpDesc &dst,
                                                        Sampling sampling) {
    if (!canRunDirectFullKernel(src, dst)) {
        return StretchBlit_Bgra8_Hwc_Rect_Rgb8_Rect_Hwc(src, dst, sampling);
    }

    (void)sampling;

    const uint8_t *srcPtr = src.data;
    const size_t srcWidth = src.surfaceWidth;

    uint8_t *dstPtr = (uint8_t *)dst.data;
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;

    if (!srcPtr || !dstPtr)
        return false;

    constexpr size_t Cdst = 3;

    for (size_t y = 0; y < dstHeight; ++y) {
        for (size_t x = 0; x < dstWidth; ++x) {
            const uint8_t *p = srcPtr + (y * srcWidth + x) * 4;
            const size_t dstIndex = (y * dstWidth + x) * Cdst;

            dstPtr[dstIndex + 0] = p[2];
            dstPtr[dstIndex + 1] = p[1];
            dstPtr[dstIndex + 2] = p[0];
        }
    }

    return true;
}

bool ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgb8_Rect_Hwc(const ImageOpDesc &src,
                                                        const ImageOpDesc &dst,
                                                        Sampling sampling) {
    const uint8_t *srcPtr = src.data;
    size_t srcWidth = src.surfaceWidth;
    size_t srcHeight = src.surfaceHeight;
    const PixelRect &srcRect = src.rect;

    uint8_t *dstPtr = (uint8_t *)dst.data;
    size_t dstWidth = dst.surfaceWidth;
    size_t dstHeight = dst.surfaceHeight;
    const PixelRect &dstRect = dst.rect;

    if (!srcPtr || !dstPtr)
        return false;

    if (srcRect.x + srcRect.width > srcWidth || srcRect.y + srcRect.height > srcHeight ||
        dstRect.x + dstRect.width > dstWidth || dstRect.y + dstRect.height > dstHeight)
        return false;

    if (dst.keepAspectRatio) {
        pek::ImageOpDesc innerDst;
        if (!makeLetterboxDestination(src, dst, innerDst)) {
            return false;
        }
        fillRgb8Hwc(dst);
        return StretchBlit_Bgra8_Hwc_Rect_Rgb8_Rect_Hwc(src, innerDst, sampling);
    }

    (void)sampling;

    constexpr size_t Cdst = 3;

    for (size_t dy = 0; dy < dstRect.height; ++dy) {
        const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
        const size_t dyi = dstRect.y + dy;

        for (size_t dx = 0; dx < dstRect.width; ++dx) {
            const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
            const size_t dxi = dstRect.x + dx;

            const size_t srcIndex = (sy * srcWidth + sx) * 4;
            const uint8_t *p = srcPtr + srcIndex;

            const size_t dstIndex = (dyi * dstWidth + dxi) * Cdst;
            // BGRA -> RGB
            dstPtr[dstIndex + 0] = p[2];
            dstPtr[dstIndex + 1] = p[1];
            dstPtr[dstIndex + 2] = p[0];
        }
    }

    return true;
}

bool ImageOps::StretchBlit_Bgra8_Hwc_Full_Rgbf16_Full_Chw(const ImageOpDesc &src,
                                                          const ImageOpDesc &dst,
                                                          Sampling sampling) {
    if (!canRunDirectFullKernel(src, dst)) {
        return StretchBlit_Bgra8_Hwc_Rect_Rgbf16_Rect_Chw(src, dst, sampling);
    }

    (void)sampling;

    const uint8_t *srcPtr = src.data;
    const size_t srcWidth = src.surfaceWidth;

    Float16 *dstPtr = (Float16 *)dst.data;
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;

    const Colorf &mean = src.mean;
    const Colorf &std = src.std;

    if (!srcPtr || !dstPtr)
        return false;

    constexpr float inv255 = 1.0f / 255.0f;
    const size_t planeSize = dstWidth * dstHeight;

    if (pek::MeanStd::isDefaultMean(mean) && pek::MeanStd::isDefaultStd(std)) {
        for (size_t y = 0; y < dstHeight; ++y) {
            for (size_t x = 0; x < dstWidth; ++x) {
                const uint8_t *p = srcPtr + (y * srcWidth + x) * 4;
                const size_t hw = y * dstWidth + x;

                dstPtr[0 * planeSize + hw] = static_cast<Float16>(p[2] * inv255);
                dstPtr[1 * planeSize + hw] = static_cast<Float16>(p[1] * inv255);
                dstPtr[2 * planeSize + hw] = static_cast<Float16>(p[0] * inv255);
            }
        }
    } else {
        const auto [meanR, meanG, meanB, meanA] = mean;
        const auto [stdR, stdG, stdB, stdA] = std;
        (void)meanA;
        (void)stdA;

        constexpr float eps = 1e-12f;
        const float invStdR = 1.0f / std::max(stdR, eps);
        const float invStdG = 1.0f / std::max(stdG, eps);
        const float invStdB = 1.0f / std::max(stdB, eps);

        for (size_t y = 0; y < dstHeight; ++y) {
            for (size_t x = 0; x < dstWidth; ++x) {
                const uint8_t *p = srcPtr + (y * srcWidth + x) * 4;
                const size_t hw = y * dstWidth + x;

                const float r01 = p[2] * inv255;
                const float g01 = p[1] * inv255;
                const float b01 = p[0] * inv255;

                dstPtr[0 * planeSize + hw] = static_cast<Float16>((r01 - meanR) * invStdR);
                dstPtr[1 * planeSize + hw] = static_cast<Float16>((g01 - meanG) * invStdG);
                dstPtr[2 * planeSize + hw] = static_cast<Float16>((b01 - meanB) * invStdB);
            }
        }
    }

    return true;
}

bool ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgbf16_Rect_Chw(const ImageOpDesc &src,
                                                          const ImageOpDesc &dst,
                                                          Sampling sampling) {
    const uint8_t *srcPtr = src.data;
    size_t srcWidth = src.surfaceWidth;
    size_t srcHeight = src.surfaceHeight;
    const PixelRect &srcRect = src.rect;

    Float16 *dstPtr = (Float16 *)dst.data;
    size_t dstWidth = dst.surfaceWidth;
    size_t dstHeight = dst.surfaceHeight;
    const PixelRect &dstRect = dst.rect;

    const Colorf &mean = src.mean;
    const Colorf &std = src.std;

    if (!srcPtr || !dstPtr)
        return false;

    if (srcRect.x + srcRect.width > srcWidth || srcRect.y + srcRect.height > srcHeight ||
        dstRect.x + dstRect.width > dstWidth || dstRect.y + dstRect.height > dstHeight)
        return false;

    if (dst.keepAspectRatio) {
        pek::ImageOpDesc innerDst;
        if (!makeLetterboxDestination(src, dst, innerDst)) {
            return false;
        }
        fillRgbF16Chw(dst, mean, std);
        return StretchBlit_Bgra8_Hwc_Rect_Rgbf16_Rect_Chw(src, innerDst, sampling);
    }

    const uint8_t *in = srcPtr;
    Float16 *out = dstPtr;
    constexpr float inv255 = 1.0f / 255.0f;

    const size_t H = dstHeight;
    const size_t W = dstWidth;

    const size_t planeSize = H * W;

    (void)sampling;

    // nearest-neighbour sampling stretch from srcRect to dstRect, CHW layout

    if (pek::MeanStd::isDefaultMean(mean) && pek::MeanStd::isDefaultStd(std)) {
        // default mean/std
        for (size_t dy = 0; dy < dstRect.height; ++dy) {
            const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
            const size_t dyi = dstRect.y + dy;

            for (size_t dx = 0; dx < dstRect.width; ++dx) {
                const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
                const size_t dxi = dstRect.x + dx;

                // source: BGRA interleaved, HWC
                const size_t srcIndexRGB = (sy * srcWidth + sx) * 4;
                const uint8_t *p = in + srcIndexRGB;

                // CHW destination
                const size_t hwIndex = dyi * W + dxi;

                out[0 * planeSize + hwIndex] = static_cast<Float16>(p[2] * inv255); // R channel
                out[1 * planeSize + hwIndex] = static_cast<Float16>(p[1] * inv255); // G channel
                out[2 * planeSize + hwIndex] = static_cast<Float16>(p[0] * inv255); // B channel
            }
        }
    } else {
        // non-default mean/std: (x - mean) / std, where x is in [0,1]
        const auto [meanR, meanG, meanB, meanA] = mean;
        const auto [stdR, stdG, stdB, stdA] = std;
        (void)meanA;
        (void)stdA;

        // Guard against division by zero (or near-zero) std
        constexpr float eps = 1e-12f;
        const float invStdR = 1.0f / std::max(stdR, eps);
        const float invStdG = 1.0f / std::max(stdG, eps);
        const float invStdB = 1.0f / std::max(stdB, eps);

        for (size_t dy = 0; dy < dstRect.height; ++dy) {
            const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
            const size_t dyi = dstRect.y + dy;

            for (size_t dx = 0; dx < dstRect.width; ++dx) {
                const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
                const size_t dxi = dstRect.x + dx;

                const size_t srcIndex = (sy * srcWidth + sx) * 4;
                const uint8_t *p = in + srcIndex;

                const size_t hwIndex = dyi * W + dxi;

                const float r01 = p[2] * inv255; // BGRA -> R
                const float g01 = p[1] * inv255; //        G
                const float b01 = p[0] * inv255; //        B

                out[0 * planeSize + hwIndex] = static_cast<Float16>((r01 - meanR) * invStdR);
                out[1 * planeSize + hwIndex] = static_cast<Float16>((g01 - meanG) * invStdG);
                out[2 * planeSize + hwIndex] = static_cast<Float16>((b01 - meanB) * invStdB);
            }
        }
    }

    return true;
}

bool ImageOps::StretchBlit_Bgra8_Hwc_Full_Rgbf32_Full_Hwc(const ImageOpDesc &src,
                                                          const ImageOpDesc &dst,
                                                          Sampling sampling) {
    if (!canRunDirectFullKernel(src, dst)) {
        return StretchBlit_Bgra8_Hwc_Rect_Rgbf32_Rect_Hwc(src, dst, sampling);
    }

    (void)sampling;

    const uint8_t *srcPtr = src.data;
    const size_t srcWidth = src.surfaceWidth;

    float *dstPtr = (float *)dst.data;
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;

    const Colorf &mean = src.mean;
    const Colorf &std = src.std;

    if (!srcPtr || !dstPtr)
        return false;

    constexpr float inv255 = 1.0f / 255.0f;
    constexpr size_t C = 3;

    if (pek::MeanStd::isDefaultMean(mean) && pek::MeanStd::isDefaultStd(std)) {
        for (size_t y = 0; y < dstHeight; ++y) {
            for (size_t x = 0; x < dstWidth; ++x) {
                const uint8_t *p = srcPtr + (y * srcWidth + x) * 4;
                float *q = dstPtr + (y * dstWidth + x) * C;

                q[0] = p[2] * inv255;
                q[1] = p[1] * inv255;
                q[2] = p[0] * inv255;
            }
        }
    } else {
        const auto [meanR, meanG, meanB, meanA] = mean;
        const auto [stdR, stdG, stdB, stdA] = std;
        (void)meanA;
        (void)stdA;

        constexpr float eps = 1e-12f;
        const float invStdR = 1.0f / std::max(stdR, eps);
        const float invStdG = 1.0f / std::max(stdG, eps);
        const float invStdB = 1.0f / std::max(stdB, eps);

        for (size_t y = 0; y < dstHeight; ++y) {
            for (size_t x = 0; x < dstWidth; ++x) {
                const uint8_t *p = srcPtr + (y * srcWidth + x) * 4;
                float *q = dstPtr + (y * dstWidth + x) * C;

                q[0] = (p[2] * inv255 - meanR) * invStdR;
                q[1] = (p[1] * inv255 - meanG) * invStdG;
                q[2] = (p[0] * inv255 - meanB) * invStdB;
            }
        }
    }

    return true;
}

bool ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgbf32_Rect_Hwc(const ImageOpDesc &src,
                                                          const ImageOpDesc &dst,
                                                          Sampling sampling) {
    const uint8_t *srcPtr = src.data;
    size_t srcWidth = src.surfaceWidth;
    size_t srcHeight = src.surfaceHeight;
    const PixelRect &srcRect = src.rect;

    float *dstPtr = (float *)dst.data;
    size_t dstWidth = dst.surfaceWidth;
    size_t dstHeight = dst.surfaceHeight;
    const PixelRect &dstRect = dst.rect;

    const Colorf &mean = src.mean;
    const Colorf &std = src.std;

    if (!srcPtr || !dstPtr)
        return false;

    if (srcRect.x + srcRect.width > srcWidth || srcRect.y + srcRect.height > srcHeight ||
        dstRect.x + dstRect.width > dstWidth || dstRect.y + dstRect.height > dstHeight)
        return false;

    if (dst.keepAspectRatio) {
        pek::ImageOpDesc innerDst;
        if (!makeLetterboxDestination(src, dst, innerDst)) {
            return false;
        }
        fillRgbF32Hwc(dst, mean, std);
        return StretchBlit_Bgra8_Hwc_Rect_Rgbf32_Rect_Hwc(src, innerDst, sampling);
    }

    constexpr float inv255 = 1.0f / 255.0f;
    constexpr size_t C = 3;

    (void)sampling;

    if (pek::MeanStd::isDefaultMean(mean) && pek::MeanStd::isDefaultStd(std)) {
        for (size_t dy = 0; dy < dstRect.height; ++dy) {
            const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
            const size_t dyi = dstRect.y + dy;

            for (size_t dx = 0; dx < dstRect.width; ++dx) {
                const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
                const size_t dxi = dstRect.x + dx;

                const uint8_t *p = srcPtr + (sy * srcWidth + sx) * 4; // BGRA
                float *q = dstPtr + (dyi * dstWidth + dxi) * C;       // RGB HWC

                q[0] = p[2] * inv255; // R
                q[1] = p[1] * inv255; // G
                q[2] = p[0] * inv255; // B
            }
        }
    } else {
        const auto [meanR, meanG, meanB, meanA] = mean;
        const auto [stdR, stdG, stdB, stdA] = std;
        (void)meanA;
        (void)stdA;

        constexpr float eps = 1e-12f;
        const float invStdR = 1.0f / std::max(stdR, eps);
        const float invStdG = 1.0f / std::max(stdG, eps);
        const float invStdB = 1.0f / std::max(stdB, eps);

        for (size_t dy = 0; dy < dstRect.height; ++dy) {
            const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
            const size_t dyi = dstRect.y + dy;

            for (size_t dx = 0; dx < dstRect.width; ++dx) {
                const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
                const size_t dxi = dstRect.x + dx;

                const uint8_t *p = srcPtr + (sy * srcWidth + sx) * 4;
                float *q = dstPtr + (dyi * dstWidth + dxi) * C;

                q[0] = (p[2] * inv255 - meanR) * invStdR;
                q[1] = (p[1] * inv255 - meanG) * invStdG;
                q[2] = (p[0] * inv255 - meanB) * invStdB;
            }
        }
    }

    return true;
}

bool ImageOps::StretchBlit_Bgra8_Hwc_Full_Rgbf16_Full_Hwc(const ImageOpDesc &src,
                                                          const ImageOpDesc &dst,
                                                          Sampling sampling) {
    if (!canRunDirectFullKernel(src, dst)) {
        return StretchBlit_Bgra8_Hwc_Rect_Rgbf16_Rect_Hwc(src, dst, sampling);
    }

    (void)sampling;

    const uint8_t *srcPtr = src.data;
    const size_t srcWidth = src.surfaceWidth;

    Float16 *dstPtr = (Float16 *)dst.data;
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;

    const Colorf &mean = src.mean;
    const Colorf &std = src.std;

    if (!srcPtr || !dstPtr)
        return false;

    constexpr float inv255 = 1.0f / 255.0f;
    constexpr size_t C = 3;

    if (pek::MeanStd::isDefaultMean(mean) && pek::MeanStd::isDefaultStd(std)) {
        for (size_t y = 0; y < dstHeight; ++y) {
            for (size_t x = 0; x < dstWidth; ++x) {
                const uint8_t *p = srcPtr + (y * srcWidth + x) * 4;
                Float16 *q = dstPtr + (y * dstWidth + x) * C;

                q[0] = static_cast<Float16>(p[2] * inv255);
                q[1] = static_cast<Float16>(p[1] * inv255);
                q[2] = static_cast<Float16>(p[0] * inv255);
            }
        }
    } else {
        const auto [meanR, meanG, meanB, meanA] = mean;
        const auto [stdR, stdG, stdB, stdA] = std;
        (void)meanA;
        (void)stdA;

        constexpr float eps = 1e-12f;
        const float invStdR = 1.0f / std::max(stdR, eps);
        const float invStdG = 1.0f / std::max(stdG, eps);
        const float invStdB = 1.0f / std::max(stdB, eps);

        for (size_t y = 0; y < dstHeight; ++y) {
            for (size_t x = 0; x < dstWidth; ++x) {
                const uint8_t *p = srcPtr + (y * srcWidth + x) * 4;
                Float16 *q = dstPtr + (y * dstWidth + x) * C;

                q[0] = static_cast<Float16>((p[2] * inv255 - meanR) * invStdR);
                q[1] = static_cast<Float16>((p[1] * inv255 - meanG) * invStdG);
                q[2] = static_cast<Float16>((p[0] * inv255 - meanB) * invStdB);
            }
        }
    }

    return true;
}

bool ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgbf16_Rect_Hwc(const ImageOpDesc &src,
                                                          const ImageOpDesc &dst,
                                                          Sampling sampling) {
    const uint8_t *srcPtr = src.data;
    size_t srcWidth = src.surfaceWidth;
    size_t srcHeight = src.surfaceHeight;
    const PixelRect &srcRect = src.rect;

    Float16 *dstPtr = (Float16 *)dst.data;
    size_t dstWidth = dst.surfaceWidth;
    size_t dstHeight = dst.surfaceHeight;
    const PixelRect &dstRect = dst.rect;

    const Colorf &mean = src.mean;
    const Colorf &std = src.std;

    if (!srcPtr || !dstPtr)
        return false;

    if (srcRect.x + srcRect.width > srcWidth || srcRect.y + srcRect.height > srcHeight ||
        dstRect.x + dstRect.width > dstWidth || dstRect.y + dstRect.height > dstHeight)
        return false;

    if (dst.keepAspectRatio) {
        pek::ImageOpDesc innerDst;
        if (!makeLetterboxDestination(src, dst, innerDst)) {
            return false;
        }
        fillRgbF16Hwc(dst, mean, std);
        return StretchBlit_Bgra8_Hwc_Rect_Rgbf16_Rect_Hwc(src, innerDst, sampling);
    }

    constexpr float inv255 = 1.0f / 255.0f;
    constexpr size_t C = 3;

    (void)sampling;

    if (pek::MeanStd::isDefaultMean(mean) && pek::MeanStd::isDefaultStd(std)) {
        for (size_t dy = 0; dy < dstRect.height; ++dy) {
            const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
            const size_t dyi = dstRect.y + dy;

            for (size_t dx = 0; dx < dstRect.width; ++dx) {
                const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
                const size_t dxi = dstRect.x + dx;

                const uint8_t *p = srcPtr + (sy * srcWidth + sx) * 4;
                Float16 *q = dstPtr + (dyi * dstWidth + dxi) * C;

                q[0] = static_cast<Float16>(p[2] * inv255); // R
                q[1] = static_cast<Float16>(p[1] * inv255); // G
                q[2] = static_cast<Float16>(p[0] * inv255); // B
            }
        }
    } else {
        const auto [meanR, meanG, meanB, meanA] = mean;
        const auto [stdR, stdG, stdB, stdA] = std;
        (void)meanA;
        (void)stdA;

        constexpr float eps = 1e-12f;
        const float invStdR = 1.0f / std::max(stdR, eps);
        const float invStdG = 1.0f / std::max(stdG, eps);
        const float invStdB = 1.0f / std::max(stdB, eps);

        for (size_t dy = 0; dy < dstRect.height; ++dy) {
            const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
            const size_t dyi = dstRect.y + dy;

            for (size_t dx = 0; dx < dstRect.width; ++dx) {
                const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
                const size_t dxi = dstRect.x + dx;

                const uint8_t *p = srcPtr + (sy * srcWidth + sx) * 4;
                Float16 *q = dstPtr + (dyi * dstWidth + dxi) * C;

                q[0] = static_cast<Float16>((p[2] * inv255 - meanR) * invStdR);
                q[1] = static_cast<Float16>((p[1] * inv255 - meanG) * invStdG);
                q[2] = static_cast<Float16>((p[0] * inv255 - meanB) * invStdB);
            }
        }
    }

    return true;
}

bool ImageOps::StretchBlit_Bgra8_Hwc_Full_Gray8_Full(const ImageOpDesc &src,
                                                     const ImageOpDesc &dst,
                                                     Sampling sampling) {
    if (!canRunDirectFullKernel(src, dst)) {
        return StretchBlit_Bgra8_Hwc_Rect_Gray8_Rect(src, dst, sampling);
    }

    (void)sampling;

    const uint8_t *srcPtr = src.data;
    const size_t srcWidth = src.surfaceWidth;

    uint8_t *dstPtr = (uint8_t *)dst.data;
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;

    if (!srcPtr || !dstPtr)
        return false;

    for (size_t y = 0; y < dstHeight; ++y) {
        for (size_t x = 0; x < dstWidth; ++x) {
            const uint8_t *p = srcPtr + (y * srcWidth + x) * 4;
            const uint8_t r = p[2];
            const uint8_t g = p[1];
            const uint8_t b = p[0];
            const uint16_t yy = static_cast<uint16_t>(77u * r + 150u * g + 29u * b + 128u);
            dstPtr[y * dstWidth + x] = static_cast<uint8_t>(yy >> 8);
        }
    }

    return true;
}

bool ImageOps::StretchBlit_Bgra8_Hwc_Rect_Gray8_Rect(const ImageOpDesc &src,
                                                     const ImageOpDesc &dst,
                                                     Sampling sampling) {
    const uint8_t *srcPtr = src.data;
    size_t srcWidth = src.surfaceWidth;
    size_t srcHeight = src.surfaceHeight;
    const PixelRect &srcRect = src.rect;

    uint8_t *dstPtr = (uint8_t *)dst.data;
    size_t dstWidth = dst.surfaceWidth;
    size_t dstHeight = dst.surfaceHeight;
    const PixelRect &dstRect = dst.rect;

    if (!srcPtr || !dstPtr)
        return false;

    if (srcRect.x + srcRect.width > srcWidth || srcRect.y + srcRect.height > srcHeight ||
        dstRect.x + dstRect.width > dstWidth || dstRect.y + dstRect.height > dstHeight)
        return false;

    if (dst.keepAspectRatio) {
        pek::ImageOpDesc innerDst;
        if (!makeLetterboxDestination(src, dst, innerDst)) {
            return false;
        }
        fillGray8(dst);
        return StretchBlit_Bgra8_Hwc_Rect_Gray8_Rect(src, innerDst, sampling);
    }

    (void)sampling;

    for (size_t dy = 0; dy < dstRect.height; ++dy) {
        const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
        const size_t dyi = dstRect.y + dy;

        for (size_t dx = 0; dx < dstRect.width; ++dx) {
            const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
            const size_t dxi = dstRect.x + dx;

            const uint8_t *p = srcPtr + (sy * srcWidth + sx) * 4;
            const uint8_t r = p[2];
            const uint8_t g = p[1];
            const uint8_t b = p[0];

            const uint16_t y = static_cast<uint16_t>(77u * r + 150u * g + 29u * b + 128u);
            dstPtr[dyi * dstWidth + dxi] = static_cast<uint8_t>(y >> 8);
        }
    }

    return true;
}

bool ImageOps::StretchBlit_Bgra8_Hwc_Full_Grayf32_Full(const ImageOpDesc &src,
                                                       const ImageOpDesc &dst,
                                                       Sampling sampling) {
    if (!canRunDirectFullKernel(src, dst)) {
        return StretchBlit_Bgra8_Hwc_Rect_Grayf32_Rect(src, dst, sampling);
    }

    (void)sampling;

    const uint8_t *srcPtr = src.data;
    const size_t srcWidth = src.surfaceWidth;

    float *dstPtr = (float *)dst.data;
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;

    const Colorf &mean = src.mean;
    const Colorf &std = src.std;

    if (!srcPtr || !dstPtr)
        return false;

    constexpr float inv255 = 1.0f / 255.0f;

    if (pek::MeanStd::isDefaultMean(mean) && pek::MeanStd::isDefaultStd(std)) {
        for (size_t y = 0; y < dstHeight; ++y) {
            for (size_t x = 0; x < dstWidth; ++x) {
                const uint8_t *p = srcPtr + (y * srcWidth + x) * 4;
                const float r = static_cast<float>(p[2]) * inv255;
                const float g = static_cast<float>(p[1]) * inv255;
                const float b = static_cast<float>(p[0]) * inv255;
                dstPtr[y * dstWidth + x] = 0.299f * r + 0.587f * g + 0.114f * b;
            }
        }
    } else {
        constexpr float eps = 1e-12f;
        const float invStd = 1.0f / std::max(std.r, eps);

        for (size_t y = 0; y < dstHeight; ++y) {
            for (size_t x = 0; x < dstWidth; ++x) {
                const uint8_t *p = srcPtr + (y * srcWidth + x) * 4;
                const float r = static_cast<float>(p[2]) * inv255;
                const float g = static_cast<float>(p[1]) * inv255;
                const float b = static_cast<float>(p[0]) * inv255;
                const float gray = 0.299f * r + 0.587f * g + 0.114f * b;
                dstPtr[y * dstWidth + x] = (gray - mean.r) * invStd;
            }
        }
    }

    return true;
}

bool ImageOps::StretchBlit_Bgra8_Hwc_Rect_Grayf32_Rect(const ImageOpDesc &src,
                                                       const ImageOpDesc &dst,
                                                       Sampling sampling) {
    const uint8_t *srcPtr = src.data;
    const size_t srcWidth = src.surfaceWidth;
    const size_t srcHeight = src.surfaceHeight;
    const PixelRect &srcRect = src.rect;

    float *dstPtr = (float *)dst.data;
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;
    const PixelRect &dstRect = dst.rect;

    if (!srcPtr || !dstPtr)
        return false;

    if (srcRect.x + srcRect.width > srcWidth || srcRect.y + srcRect.height > srcHeight ||
        dstRect.x + dstRect.width > dstWidth || dstRect.y + dstRect.height > dstHeight)
        return false;

    const Colorf &mean = src.mean;
    const Colorf &std = src.std;

    if (dst.keepAspectRatio) {
        pek::ImageOpDesc innerDst;
        if (!makeLetterboxDestination(src, dst, innerDst)) {
            return false;
        }
        fillGrayF32(dst, mean, std);
        return StretchBlit_Bgra8_Hwc_Rect_Grayf32_Rect(src, innerDst, sampling);
    }

    constexpr float inv255 = 1.0f / 255.0f;

    (void)sampling;

    if (pek::MeanStd::isDefaultMean(mean) && pek::MeanStd::isDefaultStd(std)) {
        for (size_t dy = 0; dy < dstRect.height; ++dy) {
            const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
            const size_t dyi = dstRect.y + dy;

            for (size_t dx = 0; dx < dstRect.width; ++dx) {
                const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
                const size_t dxi = dstRect.x + dx;

                const uint8_t *p = srcPtr + (sy * srcWidth + sx) * 4;
                const float r = static_cast<float>(p[2]) * inv255;
                const float g = static_cast<float>(p[1]) * inv255;
                const float b = static_cast<float>(p[0]) * inv255;
                dstPtr[dyi * dstWidth + dxi] = 0.299f * r + 0.587f * g + 0.114f * b;
            }
        }
    } else {
        constexpr float eps = 1e-12f;
        const float invStd = 1.0f / std::max(std.r, eps);

        for (size_t dy = 0; dy < dstRect.height; ++dy) {
            const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
            const size_t dyi = dstRect.y + dy;

            for (size_t dx = 0; dx < dstRect.width; ++dx) {
                const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
                const size_t dxi = dstRect.x + dx;

                const uint8_t *p = srcPtr + (sy * srcWidth + sx) * 4;
                const float r = static_cast<float>(p[2]) * inv255;
                const float g = static_cast<float>(p[1]) * inv255;
                const float b = static_cast<float>(p[0]) * inv255;
                const float gray = 0.299f * r + 0.587f * g + 0.114f * b;
                dstPtr[dyi * dstWidth + dxi] = (gray - mean.r) * invStd;
            }
        }
    }

    return true;
}

bool ImageOps::StrechBlit_Rgb8_Chw_Rect_Rgbf32_Rect_Hwc(const ImageOpDesc &src,
                                                        const ImageOpDesc &dst,
                                                        Sampling sampling) {
    const uint8_t *srcPtr = src.data;
    size_t srcWidth = src.surfaceWidth;
    size_t srcHeight = src.surfaceHeight;
    const PixelRect &srcRect = src.rect;

    float *dstPtr = (float *)dst.data;
    size_t dstWidth = dst.surfaceWidth;
    size_t dstHeight = dst.surfaceHeight;
    const PixelRect &dstRect = dst.rect;

    if (!srcPtr || !dstPtr)
        return false;

    if (srcRect.x + srcRect.width > srcWidth || srcRect.y + srcRect.height > srcHeight ||
        dstRect.x + dstRect.width > dstWidth || dstRect.y + dstRect.height > dstHeight)
        return false;

    if (dst.keepAspectRatio) {
        pek::ImageOpDesc innerDst;
        if (!makeLetterboxDestination(src, dst, innerDst)) {
            return false;
        }
        fillRgbF32Hwc(dst, defaultMean, defaultStd);
        return StrechBlit_Rgb8_Chw_Rect_Rgbf32_Rect_Hwc(src, innerDst, sampling);
    }

    constexpr float inv255 = 1.0f / 255.0f;

    const size_t C = 3;
    const size_t Wdst = dstWidth;

    const size_t srcPlane = srcWidth * srcHeight; // CHW planes
    const size_t dstPlane = dstWidth * dstHeight; // not used here, but symmetry

    (void)dstPlane;
    (void)sampling;

    // nearest-neighbor resize, CHW(u8) -> HWC(f32)
    for (size_t dy = 0; dy < dstRect.height; ++dy) {
        const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
        const size_t dyi = dstRect.y + dy;

        for (size_t dx = 0; dx < dstRect.width; ++dx) {
            const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
            const size_t dxi = dstRect.x + dx;

            const size_t srcHw = sy * srcWidth + sx;

            const uint8_t r = srcPtr[0 * srcPlane + srcHw];
            const uint8_t g = srcPtr[1 * srcPlane + srcHw];
            const uint8_t b = srcPtr[2 * srcPlane + srcHw];

            const size_t dstIndex = (dyi * Wdst + dxi) * C; // HWC interleaved

            dstPtr[dstIndex + 0] = r * inv255;
            dstPtr[dstIndex + 1] = g * inv255;
            dstPtr[dstIndex + 2] = b * inv255;
        }
    }

    return true;
}

bool ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf32_Full_Hwc(const ImageOpDesc &src,
                                                        const ImageOpDesc &dst,
                                                        Sampling sampling) {
    if (!canRunDirectFullKernel(src, dst)) {
        return StrechBlit_Rgb8_Chw_Rect_Rgbf32_Rect_Hwc(src, dst, sampling);
    }

    (void)sampling;

    const uint8_t *srcPtr = src.data;
    const size_t srcWidth = src.surfaceWidth;
    const size_t srcHeight = src.surfaceHeight;

    float *dstPtr = (float *)dst.data;
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;

    if (!srcPtr || !dstPtr)
        return false;

    constexpr float inv255 = 1.0f / 255.0f;
    constexpr size_t C = 3;
    const size_t srcPlane = srcWidth * srcHeight;

    for (size_t y = 0; y < dstHeight; ++y) {
        for (size_t x = 0; x < dstWidth; ++x) {
            const size_t srcHw = y * srcWidth + x;
            const size_t dstIndex = (y * dstWidth + x) * C;
            dstPtr[dstIndex + 0] = srcPtr[0 * srcPlane + srcHw] * inv255;
            dstPtr[dstIndex + 1] = srcPtr[1 * srcPlane + srcHw] * inv255;
            dstPtr[dstIndex + 2] = srcPtr[2 * srcPlane + srcHw] * inv255;
        }
    }

    return true;
}

bool ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf16_Full_Hwc(const ImageOpDesc &src,
                                                        const ImageOpDesc &dst,
                                                        Sampling sampling) {
    if (!canRunDirectFullKernel(src, dst)) {
        return StrechBlit_Rgb8_Chw_Rect_Rgbf16_Rect_Hwc(src, dst, sampling);
    }

    (void)sampling;

    const uint8_t *srcPtr = src.data;
    const size_t srcWidth = src.surfaceWidth;
    const size_t srcHeight = src.surfaceHeight;

    Float16 *dstPtr = (Float16 *)dst.data;
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;

    if (!srcPtr || !dstPtr)
        return false;

    constexpr float inv255 = 1.0f / 255.0f;
    constexpr size_t C = 3;
    const size_t srcPlane = srcWidth * srcHeight;

    for (size_t y = 0; y < dstHeight; ++y) {
        for (size_t x = 0; x < dstWidth; ++x) {
            const size_t srcHw = y * srcWidth + x;
            const size_t dstIndex = (y * dstWidth + x) * C;
            dstPtr[dstIndex + 0] = static_cast<Float16>(srcPtr[0 * srcPlane + srcHw] * inv255);
            dstPtr[dstIndex + 1] = static_cast<Float16>(srcPtr[1 * srcPlane + srcHw] * inv255);
            dstPtr[dstIndex + 2] = static_cast<Float16>(srcPtr[2 * srcPlane + srcHw] * inv255);
        }
    }

    return true;
}

bool ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf32_Full_Chw(const ImageOpDesc &src,
                                                        const ImageOpDesc &dst,
                                                        Sampling sampling) {
    if (!canRunDirectFullKernel(src, dst)) {
        return StrechBlit_Rgb8_Chw_Rect_Rgbf32_Rect_Chw(src, dst, sampling);
    }

    (void)sampling;

    const uint8_t *srcPtr = src.data;
    const size_t srcWidth = src.surfaceWidth;
    const size_t srcHeight = src.surfaceHeight;

    float *dstPtr = (float *)dst.data;
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;

    if (!srcPtr || !dstPtr)
        return false;

    constexpr float inv255 = 1.0f / 255.0f;
    const size_t srcPlane = srcWidth * srcHeight;
    const size_t dstPlane = dstWidth * dstHeight;

    for (size_t y = 0; y < dstHeight; ++y) {
        for (size_t x = 0; x < dstWidth; ++x) {
            const size_t srcHw = y * srcWidth + x;
            const size_t dstHw = y * dstWidth + x;
            dstPtr[0 * dstPlane + dstHw] = srcPtr[0 * srcPlane + srcHw] * inv255;
            dstPtr[1 * dstPlane + dstHw] = srcPtr[1 * srcPlane + srcHw] * inv255;
            dstPtr[2 * dstPlane + dstHw] = srcPtr[2 * srcPlane + srcHw] * inv255;
        }
    }

    return true;
}

bool ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf16_Full_Chw(const ImageOpDesc &src,
                                                        const ImageOpDesc &dst,
                                                        Sampling sampling) {
    if (!canRunDirectFullKernel(src, dst)) {
        return StrechBlit_Rgb8_Chw_Rect_Rgbf16_Rect_Chw(src, dst, sampling);
    }

    (void)sampling;

    const uint8_t *srcPtr = src.data;
    const size_t srcWidth = src.surfaceWidth;
    const size_t srcHeight = src.surfaceHeight;

    Float16 *dstPtr = (Float16 *)dst.data;
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;

    if (!srcPtr || !dstPtr)
        return false;

    constexpr float inv255 = 1.0f / 255.0f;
    const size_t srcPlane = srcWidth * srcHeight;
    const size_t dstPlane = dstWidth * dstHeight;

    for (size_t y = 0; y < dstHeight; ++y) {
        for (size_t x = 0; x < dstWidth; ++x) {
            const size_t srcHw = y * srcWidth + x;
            const size_t dstHw = y * dstWidth + x;
            dstPtr[0 * dstPlane + dstHw] =
                static_cast<Float16>(srcPtr[0 * srcPlane + srcHw] * inv255);
            dstPtr[1 * dstPlane + dstHw] =
                static_cast<Float16>(srcPtr[1 * srcPlane + srcHw] * inv255);
            dstPtr[2 * dstPlane + dstHw] =
                static_cast<Float16>(srcPtr[2 * srcPlane + srcHw] * inv255);
        }
    }

    return true;
}

bool ImageOps::StrechBlit_Rgb8_Chw_Rect_Rgbf32_Rect_Chw(const ImageOpDesc &src,
                                                        const ImageOpDesc &dst,
                                                        Sampling sampling) {
    const uint8_t *srcPtr = src.data;
    size_t srcWidth = src.surfaceWidth;
    size_t srcHeight = src.surfaceHeight;
    const PixelRect &srcRect = src.rect;

    float *dstPtr = (float *)dst.data;
    size_t dstWidth = dst.surfaceWidth;
    size_t dstHeight = dst.surfaceHeight;
    const PixelRect &dstRect = dst.rect;

    if (!srcPtr || !dstPtr)
        return false;

    if (srcRect.x + srcRect.width > srcWidth || srcRect.y + srcRect.height > srcHeight ||
        dstRect.x + dstRect.width > dstWidth || dstRect.y + dstRect.height > dstHeight)
        return false;

    if (dst.keepAspectRatio) {
        pek::ImageOpDesc innerDst;
        if (!makeLetterboxDestination(src, dst, innerDst)) {
            return false;
        }
        fillRgbF32Chw(dst, defaultMean, defaultStd);
        return StrechBlit_Rgb8_Chw_Rect_Rgbf32_Rect_Chw(src, innerDst, sampling);
    }

    constexpr float inv255 = 1.0f / 255.0f;

    const size_t Wdst = dstWidth;
    const size_t Hdst = dstHeight;

    const size_t srcPlane = srcWidth * srcHeight; // CHW source planes
    const size_t dstPlane = Wdst * Hdst;          // CHW dest planes

    (void)sampling;

    // nearest-neighbor resize, CHW(u8) -> CHW(f32)
    for (size_t dy = 0; dy < dstRect.height; ++dy) {
        const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
        const size_t dyi = dstRect.y + dy;

        for (size_t dx = 0; dx < dstRect.width; ++dx) {
            const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
            const size_t dxi = dstRect.x + dx;

            const size_t srcHw = sy * srcWidth + sx;
            const size_t dstHw = dyi * Wdst + dxi;

            const uint8_t r = srcPtr[0 * srcPlane + srcHw];
            const uint8_t g = srcPtr[1 * srcPlane + srcHw];
            const uint8_t b = srcPtr[2 * srcPlane + srcHw];

            dstPtr[0 * dstPlane + dstHw] = r * inv255;
            dstPtr[1 * dstPlane + dstHw] = g * inv255;
            dstPtr[2 * dstPlane + dstHw] = b * inv255;
        }
    }

    return true;
}

bool ImageOps::StrechBlit_Rgb8_Chw_Rect_Rgbf16_Rect_Hwc(const ImageOpDesc &src,
                                                        const ImageOpDesc &dst,
                                                        Sampling sampling) {
    const uint8_t *srcPtr = src.data;
    size_t srcWidth = src.surfaceWidth;
    size_t srcHeight = src.surfaceHeight;
    const PixelRect &srcRect = src.rect;

    Float16 *dstPtr = (Float16 *)dst.data;
    size_t dstWidth = dst.surfaceWidth;
    size_t dstHeight = dst.surfaceHeight;
    const PixelRect &dstRect = dst.rect;

    if (!srcPtr || !dstPtr)
        return false;

    if (srcRect.x + srcRect.width > srcWidth || srcRect.y + srcRect.height > srcHeight ||
        dstRect.x + dstRect.width > dstWidth || dstRect.y + dstRect.height > dstHeight)
        return false;

    if (dst.keepAspectRatio) {
        pek::ImageOpDesc innerDst;
        if (!makeLetterboxDestination(src, dst, innerDst)) {
            return false;
        }
        fillRgbF16Hwc(dst, defaultMean, defaultStd);
        return StrechBlit_Rgb8_Chw_Rect_Rgbf16_Rect_Hwc(src, innerDst, sampling);
    }

    constexpr float inv255 = 1.0f / 255.0f;
    const size_t C = 3;
    const size_t Wdst = dstWidth;
    const size_t srcPlane = srcWidth * srcHeight;

    (void)sampling;

    for (size_t dy = 0; dy < dstRect.height; ++dy) {
        const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
        const size_t dyi = dstRect.y + dy;

        for (size_t dx = 0; dx < dstRect.width; ++dx) {
            const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
            const size_t dxi = dstRect.x + dx;

            const size_t srcHw = sy * srcWidth + sx;
            const uint8_t r = srcPtr[0 * srcPlane + srcHw];
            const uint8_t g = srcPtr[1 * srcPlane + srcHw];
            const uint8_t b = srcPtr[2 * srcPlane + srcHw];

            const size_t dstIndex = (dyi * Wdst + dxi) * C;
            dstPtr[dstIndex + 0] = static_cast<Float16>(r * inv255);
            dstPtr[dstIndex + 1] = static_cast<Float16>(g * inv255);
            dstPtr[dstIndex + 2] = static_cast<Float16>(b * inv255);
        }
    }

    return true;
}

bool ImageOps::StrechBlit_Rgb8_Chw_Rect_Rgbf16_Rect_Chw(const ImageOpDesc &src,
                                                        const ImageOpDesc &dst,
                                                        Sampling sampling) {
    const uint8_t *srcPtr = src.data;
    size_t srcWidth = src.surfaceWidth;
    size_t srcHeight = src.surfaceHeight;
    const PixelRect &srcRect = src.rect;

    Float16 *dstPtr = (Float16 *)dst.data;
    size_t dstWidth = dst.surfaceWidth;
    size_t dstHeight = dst.surfaceHeight;
    const PixelRect &dstRect = dst.rect;

    if (!srcPtr || !dstPtr)
        return false;

    if (srcRect.x + srcRect.width > srcWidth || srcRect.y + srcRect.height > srcHeight ||
        dstRect.x + dstRect.width > dstWidth || dstRect.y + dstRect.height > dstHeight)
        return false;

    if (dst.keepAspectRatio) {
        pek::ImageOpDesc innerDst;
        if (!makeLetterboxDestination(src, dst, innerDst)) {
            return false;
        }
        fillRgbF16Chw(dst, defaultMean, defaultStd);
        return StrechBlit_Rgb8_Chw_Rect_Rgbf16_Rect_Chw(src, innerDst, sampling);
    }

    constexpr float inv255 = 1.0f / 255.0f;
    const size_t Wdst = dstWidth;
    const size_t Hdst = dstHeight;
    const size_t srcPlane = srcWidth * srcHeight;
    const size_t dstPlane = Wdst * Hdst;

    (void)sampling;

    for (size_t dy = 0; dy < dstRect.height; ++dy) {
        const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
        const size_t dyi = dstRect.y + dy;

        for (size_t dx = 0; dx < dstRect.width; ++dx) {
            const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
            const size_t dxi = dstRect.x + dx;

            const size_t srcHw = sy * srcWidth + sx;
            const size_t dstHw = dyi * Wdst + dxi;

            const uint8_t r = srcPtr[0 * srcPlane + srcHw];
            const uint8_t g = srcPtr[1 * srcPlane + srcHw];
            const uint8_t b = srcPtr[2 * srcPlane + srcHw];

            dstPtr[0 * dstPlane + dstHw] = static_cast<Float16>(r * inv255);
            dstPtr[1 * dstPlane + dstHw] = static_cast<Float16>(g * inv255);
            dstPtr[2 * dstPlane + dstHw] = static_cast<Float16>(b * inv255);
        }
    }

    return true;
}
