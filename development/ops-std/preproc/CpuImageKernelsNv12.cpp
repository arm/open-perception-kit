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

#include "preproc/CpuImageKernelsNv12.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

using namespace opk::stdop::preproc;
using opk::Float16;

namespace {

struct Rgbf {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

struct Nv12SourceView {
    const uint8_t *y = nullptr;
    const uint8_t *uv = nullptr;
    size_t yStrideBytes = 0;
    size_t uvStrideBytes = 0;
};

struct YuvToRgb {
    float yOffset = 16.0f;
    float yScale = 1.0f / 219.0f;
    float uvOffset = 128.0f;
    float uvScale = 1.0f / 224.0f;
    float rFromCr = 0.0f;
    float bFromCb = 0.0f;
    float gFromCb = 0.0f;
    float gFromCr = 0.0f;
};

bool multiplyOverflows(size_t a, size_t b) {
    return a != 0 && b > std::numeric_limits<size_t>::max() / a;
}

bool hasPlaneBytes(const opk::ImagePlaneDesc &plane, size_t width, size_t height) {
    if (!plane.data || width == 0 || height == 0) {
        return false;
    }
    if (plane.mutableData != nullptr)
        return false;

    const size_t strideBytes = plane.strideBytes != 0 ? plane.strideBytes : width;
    if (strideBytes < width) {
        return false;
    }

    if (plane.byteCount != 0) {
        const size_t lastRow = height - 1;
        if (multiplyOverflows(lastRow, strideBytes)) {
            return false;
        }

        const size_t lastRowOffset = lastRow * strideBytes;
        if (lastRowOffset > std::numeric_limits<size_t>::max() - width) {
            return false;
        }

        if (plane.byteCount < lastRowOffset + width) {
            return false;
        }
    }

    return true;
}

Nv12SourceView makeNv12SourceView(const opk::ImageOpDesc &src) {
    if (src.planeCount < 2 || src.surfaceWidth == 0 || src.surfaceHeight == 0) {
        return {};
    }

    const size_t chromaWidth = (src.surfaceWidth + 1) / 2;
    const size_t chromaHeight = (src.surfaceHeight + 1) / 2;
    const size_t chromaRowBytes = chromaWidth * 2;

    if (!hasPlaneBytes(src.planes[0], src.surfaceWidth, src.surfaceHeight) ||
        !hasPlaneBytes(src.planes[1], chromaRowBytes, chromaHeight)) {
        return {};
    }

    return {
        src.planes[0].data,
        src.planes[1].data,
        src.planes[0].strideBytes != 0 ? src.planes[0].strideBytes : src.surfaceWidth,
        src.planes[1].strideBytes != 0 ? src.planes[1].strideBytes : chromaRowBytes,
    };
}

opk::YuvColorMatrix resolveYuvMatrix(const opk::ImageOpDesc &src) {
    using enum opk::YuvColorMatrix;

    if (src.yuvMatrix != Unknown) {
        return src.yuvMatrix;
    }

    return src.surfaceHeight <= 576 ? Bt601 : Bt709;
}

opk::YuvRange resolveYuvRange(const opk::ImageOpDesc &src) {
    using enum opk::YuvRange;

    if (src.yuvRange != Unknown) {
        return src.yuvRange;
    }

    return Limited;
}

YuvToRgb makeYuvToRgb(const opk::ImageOpDesc &src) {
    const auto coefficients = opk::getYuvToRgbCoefficients(resolveYuvMatrix(src));
    const float kr = coefficients.kr;
    const float kb = coefficients.kb;
    const float kg = 1.0f - kr - kb;

    YuvToRgb conversion;
    using enum opk::YuvRange;
    if (resolveYuvRange(src) == Full) {
        conversion.yOffset = 0.0f;
        conversion.yScale = 1.0f / 255.0f;
        conversion.uvScale = 1.0f / 255.0f;
    }

    conversion.rFromCr = 2.0f * (1.0f - kr);
    conversion.bFromCb = 2.0f * (1.0f - kb);
    conversion.gFromCb = kb * conversion.bFromCb / kg;
    conversion.gFromCr = kr * conversion.rFromCr / kg;
    return conversion;
}

bool hasTightDestinationStride(const opk::ImageOpDesc &dst) {
    return dst.planes[0].data == nullptr && dst.planes[0].mutableData != nullptr &&
           dst.planes[0].strideBytes == 0;
}

const uint8_t *planeRowAt(const uint8_t *plane, size_t strideBytes, size_t y) {
    return plane + y * strideBytes;
}

inline bool canRunDirectFullKernel(const opk::ImageOpDesc &src, const opk::ImageOpDesc &dst) {
    return !dst.keepAspectRatio && src.rectIsFullSurface() && dst.rectIsFullSurface() &&
           src.surfaceWidth == dst.surfaceWidth && src.surfaceHeight == dst.surfaceHeight;
}

Rgbf letterboxRgb(const opk::ImageOpDesc &dst) {
    return Rgbf{
        std::clamp(dst.letterboxRed, 0.0f, 1.0f),
        std::clamp(dst.letterboxGreen, 0.0f, 1.0f),
        std::clamp(dst.letterboxBlue, 0.0f, 1.0f),
    };
}

Rgbf applyMeanStd(const Rgbf &rgb, const opk::Colorf &mean, const opk::Colorf &std) {
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

bool makeLetterboxDestination(const opk::ImageOpDesc &src,
                              const opk::ImageOpDesc &dst,
                              opk::ImageOpDesc &innerDst) {
    innerDst = dst;
    innerDst.rect = opk::computeLetterboxInnerRect(src.rect, dst.rect);
    innerDst.keepAspectRatio = false;
    return !innerDst.rect.isEmpty();
}

void fillRgbF32Chw(const opk::ImageOpDesc &dst, const opk::Colorf &mean, const opk::Colorf &std) {
    auto *out = opk::mutablePlaneData<float>(dst.planes[0]);
    const size_t planeSize = dst.surfaceWidth * dst.surfaceHeight;
    auto rgb = letterboxRgb(dst);
    if (!opk::MeanStd::isDefaultMean(mean) || !opk::MeanStd::isDefaultStd(std)) {
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

void fillRgbF16Chw(const opk::ImageOpDesc &dst, const opk::Colorf &mean, const opk::Colorf &std) {
    auto *out = opk::mutablePlaneData<Float16>(dst.planes[0]);
    const size_t planeSize = dst.surfaceWidth * dst.surfaceHeight;
    auto rgb = letterboxRgb(dst);
    if (!opk::MeanStd::isDefaultMean(mean) || !opk::MeanStd::isDefaultStd(std)) {
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

void fillRgb8Hwc(const opk::ImageOpDesc &dst) {
    auto *out = opk::mutablePlaneData<uint8_t>(dst.planes[0]);
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

void fillRgbF32Hwc(const opk::ImageOpDesc &dst, const opk::Colorf &mean, const opk::Colorf &std) {
    auto *out = opk::mutablePlaneData<float>(dst.planes[0]);
    auto rgb = letterboxRgb(dst);
    if (!opk::MeanStd::isDefaultMean(mean) || !opk::MeanStd::isDefaultStd(std)) {
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

void fillRgbF16Hwc(const opk::ImageOpDesc &dst, const opk::Colorf &mean, const opk::Colorf &std) {
    auto *out = opk::mutablePlaneData<Float16>(dst.planes[0]);
    auto rgb = letterboxRgb(dst);
    if (!opk::MeanStd::isDefaultMean(mean) || !opk::MeanStd::isDefaultStd(std)) {
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

void fillGray8(const opk::ImageOpDesc &dst) {
    auto *out = opk::mutablePlaneData<uint8_t>(dst.planes[0]);
    const uint8_t gray = rgbToGrayByte(letterboxRgb(dst));

    for (size_t y = 0; y < dst.rect.height; ++y) {
        const size_t dyi = dst.rect.y + y;
        for (size_t x = 0; x < dst.rect.width; ++x) {
            const size_t dxi = dst.rect.x + x;
            out[dyi * dst.surfaceWidth + dxi] = gray;
        }
    }
}

void fillGrayF32(const opk::ImageOpDesc &dst, const opk::Colorf &mean, const opk::Colorf &std) {
    auto *out = opk::mutablePlaneData<float>(dst.planes[0]);
    float gray = rgbToGrayFloat(letterboxRgb(dst));
    if (!opk::MeanStd::isDefaultMean(mean) || !opk::MeanStd::isDefaultStd(std)) {
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

#define OPK_READ_NV12_RGB01(yRow, uvRow, x, yuv, rOut, gOut, bOut)                                 \
    do {                                                                                           \
        const size_t chromaX = ((x) / 2) * 2;                                                      \
        const float yValue = (static_cast<float>((yRow)[x]) - (yuv).yOffset) * (yuv).yScale;       \
        const float cbValue =                                                                      \
            (static_cast<float>((uvRow)[chromaX]) - (yuv).uvOffset) * (yuv).uvScale;               \
        const float crValue =                                                                      \
            (static_cast<float>((uvRow)[chromaX + 1]) - (yuv).uvOffset) * (yuv).uvScale;           \
        (rOut) = std::clamp(yValue + (yuv).rFromCr * crValue, 0.0f, 1.0f);                         \
        (gOut) =                                                                                   \
            std::clamp(yValue - (yuv).gFromCb * cbValue - (yuv).gFromCr * crValue, 0.0f, 1.0f);    \
        (bOut) = std::clamp(yValue + (yuv).bFromCb * cbValue, 0.0f, 1.0f);                         \
    } while (false)

} // namespace

bool ImageOpsNv12::StretchBlit_Nv12_Full_Rgbf32_Full_Chw(const ImageOpDesc &src,
                                                         const ImageOpDesc &dst,
                                                         Sampling sampling) {
    if (!canRunDirectFullKernel(src, dst)) {
        return StretchBlit_Nv12_Rect_Rgbf32_Rect_Chw(src, dst, sampling);
    }

    (void)sampling;

    const auto srcView = makeNv12SourceView(src);

    float *dstPtr = opk::mutablePlaneData<float>(dst.planes[0]);
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;

    const Colorf &mean = src.mean;
    const Colorf &std = src.std;

    if (!srcView.y || !dstPtr || !hasTightDestinationStride(dst)) {
        return false;
    }

    const auto yuv = makeYuvToRgb(src);
    const size_t planeSize = dstWidth * dstHeight;

    if (opk::MeanStd::isDefaultMean(mean) && opk::MeanStd::isDefaultStd(std)) {
        for (size_t y = 0; y < dstHeight; ++y) {
            const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, y);
            const uint8_t *srcUVRow = planeRowAt(srcView.uv, srcView.uvStrideBytes, y / 2);
            for (size_t x = 0; x < dstWidth; ++x) {
                float r01 = 0.0f;
                float g01 = 0.0f;
                float b01 = 0.0f;
                OPK_READ_NV12_RGB01(srcYRow, srcUVRow, x, yuv, r01, g01, b01);

                const size_t hw = y * dstWidth + x;
                dstPtr[0 * planeSize + hw] = r01;
                dstPtr[1 * planeSize + hw] = g01;
                dstPtr[2 * planeSize + hw] = b01;
            }
        }
    } else {
        constexpr float eps = 1e-12f;
        const float invStdR = 1.0f / std::max(std.r, eps);
        const float invStdG = 1.0f / std::max(std.g, eps);
        const float invStdB = 1.0f / std::max(std.b, eps);

        for (size_t y = 0; y < dstHeight; ++y) {
            const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, y);
            const uint8_t *srcUVRow = planeRowAt(srcView.uv, srcView.uvStrideBytes, y / 2);
            for (size_t x = 0; x < dstWidth; ++x) {
                float r01 = 0.0f;
                float g01 = 0.0f;
                float b01 = 0.0f;
                OPK_READ_NV12_RGB01(srcYRow, srcUVRow, x, yuv, r01, g01, b01);

                const size_t hw = y * dstWidth + x;
                dstPtr[0 * planeSize + hw] = (r01 - mean.r) * invStdR;
                dstPtr[1 * planeSize + hw] = (g01 - mean.g) * invStdG;
                dstPtr[2 * planeSize + hw] = (b01 - mean.b) * invStdB;
            }
        }
    }

    return true;
}

bool ImageOpsNv12::StretchBlit_Nv12_Rect_Rgbf32_Rect_Chw(const ImageOpDesc &src,
                                                         const ImageOpDesc &dst,
                                                         Sampling sampling) {
    const auto srcView = makeNv12SourceView(src);
    const size_t srcWidth = src.surfaceWidth;
    const size_t srcHeight = src.surfaceHeight;
    const PixelRect &srcRect = src.rect;

    float *dstPtr = opk::mutablePlaneData<float>(dst.planes[0]);
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;
    const PixelRect &dstRect = dst.rect;

    const Colorf &mean = src.mean;
    const Colorf &std = src.std;

    if (!srcView.y || !dstPtr || !hasTightDestinationStride(dst)) {
        return false;
    }

    if (srcRect.x + srcRect.width > srcWidth || srcRect.y + srcRect.height > srcHeight ||
        dstRect.x + dstRect.width > dstWidth || dstRect.y + dstRect.height > dstHeight) {
        return false;
    }

    if (dst.keepAspectRatio) {
        opk::ImageOpDesc innerDst;
        if (!makeLetterboxDestination(src, dst, innerDst)) {
            return false;
        }
        fillRgbF32Chw(dst, mean, std);
        return StretchBlit_Nv12_Rect_Rgbf32_Rect_Chw(src, innerDst, sampling);
    }

    (void)sampling;

    const auto yuv = makeYuvToRgb(src);
    const size_t planeSize = dstHeight * dstWidth;

    if (opk::MeanStd::isDefaultMean(mean) && opk::MeanStd::isDefaultStd(std)) {
        for (size_t dy = 0; dy < dstRect.height; ++dy) {
            const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
            const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, sy);
            const uint8_t *srcUVRow = planeRowAt(srcView.uv, srcView.uvStrideBytes, sy / 2);
            const size_t dyi = dstRect.y + dy;

            for (size_t dx = 0; dx < dstRect.width; ++dx) {
                const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
                const size_t dxi = dstRect.x + dx;

                float r01 = 0.0f;
                float g01 = 0.0f;
                float b01 = 0.0f;
                OPK_READ_NV12_RGB01(srcYRow, srcUVRow, sx, yuv, r01, g01, b01);

                const size_t hw = dyi * dstWidth + dxi;
                dstPtr[0 * planeSize + hw] = r01;
                dstPtr[1 * planeSize + hw] = g01;
                dstPtr[2 * planeSize + hw] = b01;
            }
        }
    } else {
        constexpr float eps = 1e-12f;
        const float invStdR = 1.0f / std::max(std.r, eps);
        const float invStdG = 1.0f / std::max(std.g, eps);
        const float invStdB = 1.0f / std::max(std.b, eps);

        for (size_t dy = 0; dy < dstRect.height; ++dy) {
            const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
            const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, sy);
            const uint8_t *srcUVRow = planeRowAt(srcView.uv, srcView.uvStrideBytes, sy / 2);
            const size_t dyi = dstRect.y + dy;

            for (size_t dx = 0; dx < dstRect.width; ++dx) {
                const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
                const size_t dxi = dstRect.x + dx;

                float r01 = 0.0f;
                float g01 = 0.0f;
                float b01 = 0.0f;
                OPK_READ_NV12_RGB01(srcYRow, srcUVRow, sx, yuv, r01, g01, b01);

                const size_t hw = dyi * dstWidth + dxi;
                dstPtr[0 * planeSize + hw] = (r01 - mean.r) * invStdR;
                dstPtr[1 * planeSize + hw] = (g01 - mean.g) * invStdG;
                dstPtr[2 * planeSize + hw] = (b01 - mean.b) * invStdB;
            }
        }
    }

    return true;
}

bool ImageOpsNv12::StretchBlit_Nv12_Full_Rgb8_Full_Hwc(const ImageOpDesc &src,
                                                       const ImageOpDesc &dst,
                                                       Sampling sampling) {
    if (!canRunDirectFullKernel(src, dst)) {
        return StretchBlit_Nv12_Rect_Rgb8_Rect_Hwc(src, dst, sampling);
    }

    (void)sampling;

    const auto srcView = makeNv12SourceView(src);

    uint8_t *dstPtr = opk::mutablePlaneData<uint8_t>(dst.planes[0]);
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;

    if (!srcView.y || !dstPtr || !hasTightDestinationStride(dst)) {
        return false;
    }

    const auto yuv = makeYuvToRgb(src);
    constexpr size_t Cdst = 3;

    for (size_t y = 0; y < dstHeight; ++y) {
        const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, y);
        const uint8_t *srcUVRow = planeRowAt(srcView.uv, srcView.uvStrideBytes, y / 2);
        for (size_t x = 0; x < dstWidth; ++x) {
            float r01 = 0.0f;
            float g01 = 0.0f;
            float b01 = 0.0f;
            OPK_READ_NV12_RGB01(srcYRow, srcUVRow, x, yuv, r01, g01, b01);

            const size_t dstIndex = (y * dstWidth + x) * Cdst;
            dstPtr[dstIndex + 0] = toByte(r01);
            dstPtr[dstIndex + 1] = toByte(g01);
            dstPtr[dstIndex + 2] = toByte(b01);
        }
    }

    return true;
}

bool ImageOpsNv12::StretchBlit_Nv12_Rect_Rgb8_Rect_Hwc(const ImageOpDesc &src,
                                                       const ImageOpDesc &dst,
                                                       Sampling sampling) {
    const auto srcView = makeNv12SourceView(src);
    const size_t srcWidth = src.surfaceWidth;
    const size_t srcHeight = src.surfaceHeight;
    const PixelRect &srcRect = src.rect;

    uint8_t *dstPtr = opk::mutablePlaneData<uint8_t>(dst.planes[0]);
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;
    const PixelRect &dstRect = dst.rect;

    if (!srcView.y || !dstPtr || !hasTightDestinationStride(dst)) {
        return false;
    }

    if (srcRect.x + srcRect.width > srcWidth || srcRect.y + srcRect.height > srcHeight ||
        dstRect.x + dstRect.width > dstWidth || dstRect.y + dstRect.height > dstHeight) {
        return false;
    }

    if (dst.keepAspectRatio) {
        opk::ImageOpDesc innerDst;
        if (!makeLetterboxDestination(src, dst, innerDst)) {
            return false;
        }
        fillRgb8Hwc(dst);
        return StretchBlit_Nv12_Rect_Rgb8_Rect_Hwc(src, innerDst, sampling);
    }

    (void)sampling;

    const auto yuv = makeYuvToRgb(src);
    constexpr size_t Cdst = 3;

    for (size_t dy = 0; dy < dstRect.height; ++dy) {
        const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
        const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, sy);
        const uint8_t *srcUVRow = planeRowAt(srcView.uv, srcView.uvStrideBytes, sy / 2);
        const size_t dyi = dstRect.y + dy;

        for (size_t dx = 0; dx < dstRect.width; ++dx) {
            const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
            const size_t dxi = dstRect.x + dx;

            float r01 = 0.0f;
            float g01 = 0.0f;
            float b01 = 0.0f;
            OPK_READ_NV12_RGB01(srcYRow, srcUVRow, sx, yuv, r01, g01, b01);

            const size_t dstIndex = (dyi * dstWidth + dxi) * Cdst;
            dstPtr[dstIndex + 0] = toByte(r01);
            dstPtr[dstIndex + 1] = toByte(g01);
            dstPtr[dstIndex + 2] = toByte(b01);
        }
    }

    return true;
}

bool ImageOpsNv12::StretchBlit_Nv12_Full_Rgbf16_Full_Chw(const ImageOpDesc &src,
                                                         const ImageOpDesc &dst,
                                                         Sampling sampling) {
    if (!canRunDirectFullKernel(src, dst)) {
        return StretchBlit_Nv12_Rect_Rgbf16_Rect_Chw(src, dst, sampling);
    }

    (void)sampling;

    const auto srcView = makeNv12SourceView(src);

    Float16 *dstPtr = opk::mutablePlaneData<Float16>(dst.planes[0]);
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;

    const Colorf &mean = src.mean;
    const Colorf &std = src.std;

    if (!srcView.y || !dstPtr || !hasTightDestinationStride(dst)) {
        return false;
    }

    const auto yuv = makeYuvToRgb(src);
    const size_t planeSize = dstWidth * dstHeight;

    if (opk::MeanStd::isDefaultMean(mean) && opk::MeanStd::isDefaultStd(std)) {
        for (size_t y = 0; y < dstHeight; ++y) {
            const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, y);
            const uint8_t *srcUVRow = planeRowAt(srcView.uv, srcView.uvStrideBytes, y / 2);
            for (size_t x = 0; x < dstWidth; ++x) {
                float r01 = 0.0f;
                float g01 = 0.0f;
                float b01 = 0.0f;
                OPK_READ_NV12_RGB01(srcYRow, srcUVRow, x, yuv, r01, g01, b01);

                const size_t hw = y * dstWidth + x;
                dstPtr[0 * planeSize + hw] = static_cast<Float16>(r01);
                dstPtr[1 * planeSize + hw] = static_cast<Float16>(g01);
                dstPtr[2 * planeSize + hw] = static_cast<Float16>(b01);
            }
        }
    } else {
        constexpr float eps = 1e-12f;
        const float invStdR = 1.0f / std::max(std.r, eps);
        const float invStdG = 1.0f / std::max(std.g, eps);
        const float invStdB = 1.0f / std::max(std.b, eps);

        for (size_t y = 0; y < dstHeight; ++y) {
            const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, y);
            const uint8_t *srcUVRow = planeRowAt(srcView.uv, srcView.uvStrideBytes, y / 2);
            for (size_t x = 0; x < dstWidth; ++x) {
                float r01 = 0.0f;
                float g01 = 0.0f;
                float b01 = 0.0f;
                OPK_READ_NV12_RGB01(srcYRow, srcUVRow, x, yuv, r01, g01, b01);

                const size_t hw = y * dstWidth + x;
                dstPtr[0 * planeSize + hw] = static_cast<Float16>((r01 - mean.r) * invStdR);
                dstPtr[1 * planeSize + hw] = static_cast<Float16>((g01 - mean.g) * invStdG);
                dstPtr[2 * planeSize + hw] = static_cast<Float16>((b01 - mean.b) * invStdB);
            }
        }
    }

    return true;
}

bool ImageOpsNv12::StretchBlit_Nv12_Rect_Rgbf16_Rect_Chw(const ImageOpDesc &src,
                                                         const ImageOpDesc &dst,
                                                         Sampling sampling) {
    const auto srcView = makeNv12SourceView(src);
    const size_t srcWidth = src.surfaceWidth;
    const size_t srcHeight = src.surfaceHeight;
    const PixelRect &srcRect = src.rect;

    Float16 *dstPtr = opk::mutablePlaneData<Float16>(dst.planes[0]);
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;
    const PixelRect &dstRect = dst.rect;

    const Colorf &mean = src.mean;
    const Colorf &std = src.std;

    if (!srcView.y || !dstPtr || !hasTightDestinationStride(dst)) {
        return false;
    }

    if (srcRect.x + srcRect.width > srcWidth || srcRect.y + srcRect.height > srcHeight ||
        dstRect.x + dstRect.width > dstWidth || dstRect.y + dstRect.height > dstHeight) {
        return false;
    }

    if (dst.keepAspectRatio) {
        opk::ImageOpDesc innerDst;
        if (!makeLetterboxDestination(src, dst, innerDst)) {
            return false;
        }
        fillRgbF16Chw(dst, mean, std);
        return StretchBlit_Nv12_Rect_Rgbf16_Rect_Chw(src, innerDst, sampling);
    }

    (void)sampling;

    const auto yuv = makeYuvToRgb(src);
    const size_t planeSize = dstHeight * dstWidth;

    if (opk::MeanStd::isDefaultMean(mean) && opk::MeanStd::isDefaultStd(std)) {
        for (size_t dy = 0; dy < dstRect.height; ++dy) {
            const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
            const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, sy);
            const uint8_t *srcUVRow = planeRowAt(srcView.uv, srcView.uvStrideBytes, sy / 2);
            const size_t dyi = dstRect.y + dy;

            for (size_t dx = 0; dx < dstRect.width; ++dx) {
                const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
                const size_t dxi = dstRect.x + dx;

                float r01 = 0.0f;
                float g01 = 0.0f;
                float b01 = 0.0f;
                OPK_READ_NV12_RGB01(srcYRow, srcUVRow, sx, yuv, r01, g01, b01);

                const size_t hw = dyi * dstWidth + dxi;
                dstPtr[0 * planeSize + hw] = static_cast<Float16>(r01);
                dstPtr[1 * planeSize + hw] = static_cast<Float16>(g01);
                dstPtr[2 * planeSize + hw] = static_cast<Float16>(b01);
            }
        }
    } else {
        constexpr float eps = 1e-12f;
        const float invStdR = 1.0f / std::max(std.r, eps);
        const float invStdG = 1.0f / std::max(std.g, eps);
        const float invStdB = 1.0f / std::max(std.b, eps);

        for (size_t dy = 0; dy < dstRect.height; ++dy) {
            const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
            const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, sy);
            const uint8_t *srcUVRow = planeRowAt(srcView.uv, srcView.uvStrideBytes, sy / 2);
            const size_t dyi = dstRect.y + dy;

            for (size_t dx = 0; dx < dstRect.width; ++dx) {
                const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
                const size_t dxi = dstRect.x + dx;

                float r01 = 0.0f;
                float g01 = 0.0f;
                float b01 = 0.0f;
                OPK_READ_NV12_RGB01(srcYRow, srcUVRow, sx, yuv, r01, g01, b01);

                const size_t hw = dyi * dstWidth + dxi;
                dstPtr[0 * planeSize + hw] = static_cast<Float16>((r01 - mean.r) * invStdR);
                dstPtr[1 * planeSize + hw] = static_cast<Float16>((g01 - mean.g) * invStdG);
                dstPtr[2 * planeSize + hw] = static_cast<Float16>((b01 - mean.b) * invStdB);
            }
        }
    }

    return true;
}

bool ImageOpsNv12::StretchBlit_Nv12_Full_Rgbf32_Full_Hwc(const ImageOpDesc &src,
                                                         const ImageOpDesc &dst,
                                                         Sampling sampling) {
    if (!canRunDirectFullKernel(src, dst)) {
        return StretchBlit_Nv12_Rect_Rgbf32_Rect_Hwc(src, dst, sampling);
    }

    (void)sampling;

    const auto srcView = makeNv12SourceView(src);

    float *dstPtr = opk::mutablePlaneData<float>(dst.planes[0]);
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;

    const Colorf &mean = src.mean;
    const Colorf &std = src.std;

    if (!srcView.y || !dstPtr || !hasTightDestinationStride(dst)) {
        return false;
    }

    const auto yuv = makeYuvToRgb(src);
    constexpr size_t C = 3;

    if (opk::MeanStd::isDefaultMean(mean) && opk::MeanStd::isDefaultStd(std)) {
        for (size_t y = 0; y < dstHeight; ++y) {
            const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, y);
            const uint8_t *srcUVRow = planeRowAt(srcView.uv, srcView.uvStrideBytes, y / 2);
            for (size_t x = 0; x < dstWidth; ++x) {
                float r01 = 0.0f;
                float g01 = 0.0f;
                float b01 = 0.0f;
                OPK_READ_NV12_RGB01(srcYRow, srcUVRow, x, yuv, r01, g01, b01);

                float *q = dstPtr + (y * dstWidth + x) * C;
                q[0] = r01;
                q[1] = g01;
                q[2] = b01;
            }
        }
    } else {
        constexpr float eps = 1e-12f;
        const float invStdR = 1.0f / std::max(std.r, eps);
        const float invStdG = 1.0f / std::max(std.g, eps);
        const float invStdB = 1.0f / std::max(std.b, eps);

        for (size_t y = 0; y < dstHeight; ++y) {
            const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, y);
            const uint8_t *srcUVRow = planeRowAt(srcView.uv, srcView.uvStrideBytes, y / 2);
            for (size_t x = 0; x < dstWidth; ++x) {
                float r01 = 0.0f;
                float g01 = 0.0f;
                float b01 = 0.0f;
                OPK_READ_NV12_RGB01(srcYRow, srcUVRow, x, yuv, r01, g01, b01);

                float *q = dstPtr + (y * dstWidth + x) * C;
                q[0] = (r01 - mean.r) * invStdR;
                q[1] = (g01 - mean.g) * invStdG;
                q[2] = (b01 - mean.b) * invStdB;
            }
        }
    }

    return true;
}

bool ImageOpsNv12::StretchBlit_Nv12_Rect_Rgbf32_Rect_Hwc(const ImageOpDesc &src,
                                                         const ImageOpDesc &dst,
                                                         Sampling sampling) {
    const auto srcView = makeNv12SourceView(src);
    const size_t srcWidth = src.surfaceWidth;
    const size_t srcHeight = src.surfaceHeight;
    const PixelRect &srcRect = src.rect;

    float *dstPtr = opk::mutablePlaneData<float>(dst.planes[0]);
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;
    const PixelRect &dstRect = dst.rect;

    const Colorf &mean = src.mean;
    const Colorf &std = src.std;

    if (!srcView.y || !dstPtr || !hasTightDestinationStride(dst)) {
        return false;
    }

    if (srcRect.x + srcRect.width > srcWidth || srcRect.y + srcRect.height > srcHeight ||
        dstRect.x + dstRect.width > dstWidth || dstRect.y + dstRect.height > dstHeight) {
        return false;
    }

    if (dst.keepAspectRatio) {
        opk::ImageOpDesc innerDst;
        if (!makeLetterboxDestination(src, dst, innerDst)) {
            return false;
        }
        fillRgbF32Hwc(dst, mean, std);
        return StretchBlit_Nv12_Rect_Rgbf32_Rect_Hwc(src, innerDst, sampling);
    }

    (void)sampling;

    const auto yuv = makeYuvToRgb(src);
    constexpr size_t C = 3;

    if (opk::MeanStd::isDefaultMean(mean) && opk::MeanStd::isDefaultStd(std)) {
        for (size_t dy = 0; dy < dstRect.height; ++dy) {
            const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
            const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, sy);
            const uint8_t *srcUVRow = planeRowAt(srcView.uv, srcView.uvStrideBytes, sy / 2);
            const size_t dyi = dstRect.y + dy;

            for (size_t dx = 0; dx < dstRect.width; ++dx) {
                const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
                const size_t dxi = dstRect.x + dx;

                float r01 = 0.0f;
                float g01 = 0.0f;
                float b01 = 0.0f;
                OPK_READ_NV12_RGB01(srcYRow, srcUVRow, sx, yuv, r01, g01, b01);

                float *q = dstPtr + (dyi * dstWidth + dxi) * C;
                q[0] = r01;
                q[1] = g01;
                q[2] = b01;
            }
        }
    } else {
        constexpr float eps = 1e-12f;
        const float invStdR = 1.0f / std::max(std.r, eps);
        const float invStdG = 1.0f / std::max(std.g, eps);
        const float invStdB = 1.0f / std::max(std.b, eps);

        for (size_t dy = 0; dy < dstRect.height; ++dy) {
            const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
            const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, sy);
            const uint8_t *srcUVRow = planeRowAt(srcView.uv, srcView.uvStrideBytes, sy / 2);
            const size_t dyi = dstRect.y + dy;

            for (size_t dx = 0; dx < dstRect.width; ++dx) {
                const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
                const size_t dxi = dstRect.x + dx;

                float r01 = 0.0f;
                float g01 = 0.0f;
                float b01 = 0.0f;
                OPK_READ_NV12_RGB01(srcYRow, srcUVRow, sx, yuv, r01, g01, b01);

                float *q = dstPtr + (dyi * dstWidth + dxi) * C;
                q[0] = (r01 - mean.r) * invStdR;
                q[1] = (g01 - mean.g) * invStdG;
                q[2] = (b01 - mean.b) * invStdB;
            }
        }
    }

    return true;
}

bool ImageOpsNv12::StretchBlit_Nv12_Full_Rgbf16_Full_Hwc(const ImageOpDesc &src,
                                                         const ImageOpDesc &dst,
                                                         Sampling sampling) {
    if (!canRunDirectFullKernel(src, dst)) {
        return StretchBlit_Nv12_Rect_Rgbf16_Rect_Hwc(src, dst, sampling);
    }

    (void)sampling;

    const auto srcView = makeNv12SourceView(src);

    Float16 *dstPtr = opk::mutablePlaneData<Float16>(dst.planes[0]);
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;

    const Colorf &mean = src.mean;
    const Colorf &std = src.std;

    if (!srcView.y || !dstPtr || !hasTightDestinationStride(dst)) {
        return false;
    }

    const auto yuv = makeYuvToRgb(src);
    constexpr size_t C = 3;

    if (opk::MeanStd::isDefaultMean(mean) && opk::MeanStd::isDefaultStd(std)) {
        for (size_t y = 0; y < dstHeight; ++y) {
            const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, y);
            const uint8_t *srcUVRow = planeRowAt(srcView.uv, srcView.uvStrideBytes, y / 2);
            for (size_t x = 0; x < dstWidth; ++x) {
                float r01 = 0.0f;
                float g01 = 0.0f;
                float b01 = 0.0f;
                OPK_READ_NV12_RGB01(srcYRow, srcUVRow, x, yuv, r01, g01, b01);

                Float16 *q = dstPtr + (y * dstWidth + x) * C;
                q[0] = static_cast<Float16>(r01);
                q[1] = static_cast<Float16>(g01);
                q[2] = static_cast<Float16>(b01);
            }
        }
    } else {
        constexpr float eps = 1e-12f;
        const float invStdR = 1.0f / std::max(std.r, eps);
        const float invStdG = 1.0f / std::max(std.g, eps);
        const float invStdB = 1.0f / std::max(std.b, eps);

        for (size_t y = 0; y < dstHeight; ++y) {
            const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, y);
            const uint8_t *srcUVRow = planeRowAt(srcView.uv, srcView.uvStrideBytes, y / 2);
            for (size_t x = 0; x < dstWidth; ++x) {
                float r01 = 0.0f;
                float g01 = 0.0f;
                float b01 = 0.0f;
                OPK_READ_NV12_RGB01(srcYRow, srcUVRow, x, yuv, r01, g01, b01);

                Float16 *q = dstPtr + (y * dstWidth + x) * C;
                q[0] = static_cast<Float16>((r01 - mean.r) * invStdR);
                q[1] = static_cast<Float16>((g01 - mean.g) * invStdG);
                q[2] = static_cast<Float16>((b01 - mean.b) * invStdB);
            }
        }
    }

    return true;
}

bool ImageOpsNv12::StretchBlit_Nv12_Rect_Rgbf16_Rect_Hwc(const ImageOpDesc &src,
                                                         const ImageOpDesc &dst,
                                                         Sampling sampling) {
    const auto srcView = makeNv12SourceView(src);
    const size_t srcWidth = src.surfaceWidth;
    const size_t srcHeight = src.surfaceHeight;
    const PixelRect &srcRect = src.rect;

    Float16 *dstPtr = opk::mutablePlaneData<Float16>(dst.planes[0]);
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;
    const PixelRect &dstRect = dst.rect;

    const Colorf &mean = src.mean;
    const Colorf &std = src.std;

    if (!srcView.y || !dstPtr || !hasTightDestinationStride(dst)) {
        return false;
    }

    if (srcRect.x + srcRect.width > srcWidth || srcRect.y + srcRect.height > srcHeight ||
        dstRect.x + dstRect.width > dstWidth || dstRect.y + dstRect.height > dstHeight) {
        return false;
    }

    if (dst.keepAspectRatio) {
        opk::ImageOpDesc innerDst;
        if (!makeLetterboxDestination(src, dst, innerDst)) {
            return false;
        }
        fillRgbF16Hwc(dst, mean, std);
        return StretchBlit_Nv12_Rect_Rgbf16_Rect_Hwc(src, innerDst, sampling);
    }

    (void)sampling;

    const auto yuv = makeYuvToRgb(src);
    constexpr size_t C = 3;

    if (opk::MeanStd::isDefaultMean(mean) && opk::MeanStd::isDefaultStd(std)) {
        for (size_t dy = 0; dy < dstRect.height; ++dy) {
            const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
            const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, sy);
            const uint8_t *srcUVRow = planeRowAt(srcView.uv, srcView.uvStrideBytes, sy / 2);
            const size_t dyi = dstRect.y + dy;

            for (size_t dx = 0; dx < dstRect.width; ++dx) {
                const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
                const size_t dxi = dstRect.x + dx;

                float r01 = 0.0f;
                float g01 = 0.0f;
                float b01 = 0.0f;
                OPK_READ_NV12_RGB01(srcYRow, srcUVRow, sx, yuv, r01, g01, b01);

                Float16 *q = dstPtr + (dyi * dstWidth + dxi) * C;
                q[0] = static_cast<Float16>(r01);
                q[1] = static_cast<Float16>(g01);
                q[2] = static_cast<Float16>(b01);
            }
        }
    } else {
        constexpr float eps = 1e-12f;
        const float invStdR = 1.0f / std::max(std.r, eps);
        const float invStdG = 1.0f / std::max(std.g, eps);
        const float invStdB = 1.0f / std::max(std.b, eps);

        for (size_t dy = 0; dy < dstRect.height; ++dy) {
            const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
            const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, sy);
            const uint8_t *srcUVRow = planeRowAt(srcView.uv, srcView.uvStrideBytes, sy / 2);
            const size_t dyi = dstRect.y + dy;

            for (size_t dx = 0; dx < dstRect.width; ++dx) {
                const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
                const size_t dxi = dstRect.x + dx;

                float r01 = 0.0f;
                float g01 = 0.0f;
                float b01 = 0.0f;
                OPK_READ_NV12_RGB01(srcYRow, srcUVRow, sx, yuv, r01, g01, b01);

                Float16 *q = dstPtr + (dyi * dstWidth + dxi) * C;
                q[0] = static_cast<Float16>((r01 - mean.r) * invStdR);
                q[1] = static_cast<Float16>((g01 - mean.g) * invStdG);
                q[2] = static_cast<Float16>((b01 - mean.b) * invStdB);
            }
        }
    }

    return true;
}

bool ImageOpsNv12::StretchBlit_Nv12_Full_Gray8_Full(const ImageOpDesc &src,
                                                    const ImageOpDesc &dst,
                                                    Sampling sampling) {
    if (!canRunDirectFullKernel(src, dst)) {
        return StretchBlit_Nv12_Rect_Gray8_Rect(src, dst, sampling);
    }

    (void)sampling;

    const auto srcView = makeNv12SourceView(src);

    uint8_t *dstPtr = opk::mutablePlaneData<uint8_t>(dst.planes[0]);
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;

    if (!srcView.y || !dstPtr || !hasTightDestinationStride(dst)) {
        return false;
    }

    using enum opk::YuvRange;
    if (resolveYuvRange(src) == Full) {
        for (size_t y = 0; y < dstHeight; ++y) {
            const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, y);
            for (size_t x = 0; x < dstWidth; ++x) {
                dstPtr[y * dstWidth + x] = srcYRow[x];
            }
        }
    } else {
        for (size_t y = 0; y < dstHeight; ++y) {
            const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, y);
            for (size_t x = 0; x < dstWidth; ++x) {
                const int gray = std::clamp(static_cast<int>(srcYRow[x]) - 16, 0, 219);
                dstPtr[y * dstWidth + x] = static_cast<uint8_t>((gray * 255 + 109) / 219);
            }
        }
    }

    return true;
}

bool ImageOpsNv12::StretchBlit_Nv12_Rect_Gray8_Rect(const ImageOpDesc &src,
                                                    const ImageOpDesc &dst,
                                                    Sampling sampling) {
    const auto srcView = makeNv12SourceView(src);
    const size_t srcWidth = src.surfaceWidth;
    const size_t srcHeight = src.surfaceHeight;
    const PixelRect &srcRect = src.rect;

    uint8_t *dstPtr = opk::mutablePlaneData<uint8_t>(dst.planes[0]);
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;
    const PixelRect &dstRect = dst.rect;

    if (!srcView.y || !dstPtr || !hasTightDestinationStride(dst)) {
        return false;
    }

    if (srcRect.x + srcRect.width > srcWidth || srcRect.y + srcRect.height > srcHeight ||
        dstRect.x + dstRect.width > dstWidth || dstRect.y + dstRect.height > dstHeight) {
        return false;
    }

    if (dst.keepAspectRatio) {
        opk::ImageOpDesc innerDst;
        if (!makeLetterboxDestination(src, dst, innerDst)) {
            return false;
        }
        fillGray8(dst);
        return StretchBlit_Nv12_Rect_Gray8_Rect(src, innerDst, sampling);
    }

    (void)sampling;

    using enum opk::YuvRange;
    if (resolveYuvRange(src) == Full) {
        for (size_t dy = 0; dy < dstRect.height; ++dy) {
            const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
            const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, sy);
            const size_t dyi = dstRect.y + dy;

            for (size_t dx = 0; dx < dstRect.width; ++dx) {
                const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
                const size_t dxi = dstRect.x + dx;

                dstPtr[dyi * dstWidth + dxi] = srcYRow[sx];
            }
        }
    } else {
        for (size_t dy = 0; dy < dstRect.height; ++dy) {
            const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
            const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, sy);
            const size_t dyi = dstRect.y + dy;

            for (size_t dx = 0; dx < dstRect.width; ++dx) {
                const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
                const size_t dxi = dstRect.x + dx;

                const int gray = std::clamp(static_cast<int>(srcYRow[sx]) - 16, 0, 219);
                dstPtr[dyi * dstWidth + dxi] = static_cast<uint8_t>((gray * 255 + 109) / 219);
            }
        }
    }

    return true;
}

bool ImageOpsNv12::StretchBlit_Nv12_Full_Grayf32_Full(const ImageOpDesc &src,
                                                      const ImageOpDesc &dst,
                                                      Sampling sampling) {
    if (!canRunDirectFullKernel(src, dst)) {
        return StretchBlit_Nv12_Rect_Grayf32_Rect(src, dst, sampling);
    }

    (void)sampling;

    const auto srcView = makeNv12SourceView(src);

    float *dstPtr = opk::mutablePlaneData<float>(dst.planes[0]);
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;

    const Colorf &mean = src.mean;
    const Colorf &std = src.std;

    if (!srcView.y || !dstPtr || !hasTightDestinationStride(dst)) {
        return false;
    }

    const auto yuv = makeYuvToRgb(src);

    if (opk::MeanStd::isDefaultMean(mean) && opk::MeanStd::isDefaultStd(std)) {
        for (size_t y = 0; y < dstHeight; ++y) {
            const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, y);
            for (size_t x = 0; x < dstWidth; ++x) {
                dstPtr[y * dstWidth + x] = std::clamp(
                    (static_cast<float>(srcYRow[x]) - yuv.yOffset) * yuv.yScale, 0.0f, 1.0f);
            }
        }
    } else {
        constexpr float eps = 1e-12f;
        const float invStd = 1.0f / std::max(std.r, eps);

        for (size_t y = 0; y < dstHeight; ++y) {
            const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, y);
            for (size_t x = 0; x < dstWidth; ++x) {
                const float gray = std::clamp(
                    (static_cast<float>(srcYRow[x]) - yuv.yOffset) * yuv.yScale, 0.0f, 1.0f);
                dstPtr[y * dstWidth + x] = (gray - mean.r) * invStd;
            }
        }
    }

    return true;
}

bool ImageOpsNv12::StretchBlit_Nv12_Rect_Grayf32_Rect(const ImageOpDesc &src,
                                                      const ImageOpDesc &dst,
                                                      Sampling sampling) {
    const auto srcView = makeNv12SourceView(src);
    const size_t srcWidth = src.surfaceWidth;
    const size_t srcHeight = src.surfaceHeight;
    const PixelRect &srcRect = src.rect;

    float *dstPtr = opk::mutablePlaneData<float>(dst.planes[0]);
    const size_t dstWidth = dst.surfaceWidth;
    const size_t dstHeight = dst.surfaceHeight;
    const PixelRect &dstRect = dst.rect;

    if (!srcView.y || !dstPtr || !hasTightDestinationStride(dst)) {
        return false;
    }

    if (srcRect.x + srcRect.width > srcWidth || srcRect.y + srcRect.height > srcHeight ||
        dstRect.x + dstRect.width > dstWidth || dstRect.y + dstRect.height > dstHeight) {
        return false;
    }

    const Colorf &mean = src.mean;
    const Colorf &std = src.std;

    if (dst.keepAspectRatio) {
        opk::ImageOpDesc innerDst;
        if (!makeLetterboxDestination(src, dst, innerDst)) {
            return false;
        }
        fillGrayF32(dst, mean, std);
        return StretchBlit_Nv12_Rect_Grayf32_Rect(src, innerDst, sampling);
    }

    (void)sampling;

    const auto yuv = makeYuvToRgb(src);

    if (opk::MeanStd::isDefaultMean(mean) && opk::MeanStd::isDefaultStd(std)) {
        for (size_t dy = 0; dy < dstRect.height; ++dy) {
            const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
            const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, sy);
            const size_t dyi = dstRect.y + dy;

            for (size_t dx = 0; dx < dstRect.width; ++dx) {
                const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
                const size_t dxi = dstRect.x + dx;

                dstPtr[dyi * dstWidth + dxi] = std::clamp(
                    (static_cast<float>(srcYRow[sx]) - yuv.yOffset) * yuv.yScale, 0.0f, 1.0f);
            }
        }
    } else {
        constexpr float eps = 1e-12f;
        const float invStd = 1.0f / std::max(std.r, eps);

        for (size_t dy = 0; dy < dstRect.height; ++dy) {
            const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;
            const uint8_t *srcYRow = planeRowAt(srcView.y, srcView.yStrideBytes, sy);
            const size_t dyi = dstRect.y + dy;

            for (size_t dx = 0; dx < dstRect.width; ++dx) {
                const size_t sx = srcRect.x + (dx * srcRect.width) / dstRect.width;
                const size_t dxi = dstRect.x + dx;

                const float gray = std::clamp(
                    (static_cast<float>(srcYRow[sx]) - yuv.yOffset) * yuv.yScale, 0.0f, 1.0f);
                dstPtr[dyi * dstWidth + dxi] = (gray - mean.r) * invStd;
            }
        }
    }

    return true;
}

#undef OPK_READ_NV12_RGB01
