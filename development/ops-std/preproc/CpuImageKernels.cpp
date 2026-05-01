/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#include "preproc/CpuImageKernels.h"
#include "amp/Types.h"

using namespace amp;

// this one is called
bool ImageOps::StretchBlit_Bgra8_Hwc_Full_Rgbf32_Full_Chw(const ImageLayoutDesc &src,
                                                          const ImageLayoutDesc &dst,
                                                          Sampling sampling) {
    return StretchBlit_Bgra8_Hwc_Rect_Rgbf32_Rect_Chw(src, dst, sampling);
}

bool ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgbf32_Rect_Chw(const ImageLayoutDesc &src,
                                                          const ImageLayoutDesc &dst,
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

    const uint8_t *in = srcPtr;
    float *out = dstPtr;
    constexpr float inv255 = 1.0f / 255.0f;

    const size_t C = 3;
    const size_t H = dstHeight;
    const size_t W = dstWidth;

    const size_t planeSize = H * W;

    // nearest-neighbour sampling stretch from srcRect to dstRect, CHW layout

    if (amp::MeanStd::isDefaultMean(mean) && amp::MeanStd::isDefaultStd(std)) {
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

bool ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgb8_Rect_Hwc(const ImageLayoutDesc &src,
                                                        const ImageLayoutDesc &dst,
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

bool ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgbf16_Rect_Chw(const ImageLayoutDesc &src,
                                                          const ImageLayoutDesc &dst,
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

    const uint8_t *in = srcPtr;
    Float16 *out = dstPtr;
    constexpr float inv255 = 1.0f / 255.0f;

    const size_t H = dstHeight;
    const size_t W = dstWidth;

    const size_t planeSize = H * W;

    (void)sampling;

    // nearest-neighbour sampling stretch from srcRect to dstRect, CHW layout

    if (amp::MeanStd::isDefaultMean(mean) && amp::MeanStd::isDefaultStd(std)) {
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

bool ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgbf32_Rect_Hwc(const ImageLayoutDesc &src,
                                                          const ImageLayoutDesc &dst,
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

    constexpr float inv255 = 1.0f / 255.0f;
    constexpr size_t C = 3;

    (void)sampling;

    if (amp::MeanStd::isDefaultMean(mean) && amp::MeanStd::isDefaultStd(std)) {
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

bool ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgbf16_Rect_Hwc(const ImageLayoutDesc &src,
                                                          const ImageLayoutDesc &dst,
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

    constexpr float inv255 = 1.0f / 255.0f;
    constexpr size_t C = 3;

    (void)sampling;

    if (amp::MeanStd::isDefaultMean(mean) && amp::MeanStd::isDefaultStd(std)) {
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

bool ImageOps::StretchBlit_Bgra8_Hwc_Full_Gray8_Full(const ImageLayoutDesc &src,
                                                     const ImageLayoutDesc &dst,
                                                     Sampling sampling) {
    return StretchBlit_Bgra8_Hwc_Rect_Gray8_Rect(src, dst, sampling);
}

bool ImageOps::StretchBlit_Bgra8_Hwc_Rect_Gray8_Rect(const ImageLayoutDesc &src,
                                                     const ImageLayoutDesc &dst,
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

bool ImageOps::StrechBlit_Rgb8_Chw_Rect_Rgbf32_Rect_Hwc(const ImageLayoutDesc &src,
                                                        const ImageLayoutDesc &dst,
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

bool ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf32_Full_Hwc(const ImageLayoutDesc &src,
                                                        const ImageLayoutDesc &dst,
                                                        Sampling sampling) {
    return StrechBlit_Rgb8_Chw_Rect_Rgbf32_Rect_Hwc(src, dst, sampling);
}

bool ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf16_Full_Hwc(const ImageLayoutDesc &src,
                                                        const ImageLayoutDesc &dst,
                                                        Sampling sampling) {
    const uint8_t *srcPtr = src.data;
    size_t srcWidth = src.surfaceWidth;
    size_t srcHeight = src.surfaceHeight;
    
    Float16 *dstPtr = (Float16 *)dst.data;
    size_t dstWidth = dst.surfaceWidth;
    size_t dstHeight = dst.surfaceHeight;
    
    if (!srcPtr || !dstPtr)
        return false;

    constexpr float inv255 = 1.0f / 255.0f;

    const size_t C = 3;
    const size_t Wdst = dstWidth;

    const size_t srcPlane = srcWidth * srcHeight; // CHW planes

    (void)sampling;

    // nearest-neighbor resize, CHW(u8) -> HWC(f16)
    for (size_t dy = 0; dy < dstHeight; ++dy) {
        const size_t sy = (dy * srcHeight) / dstHeight;

        for (size_t dx = 0; dx < dstWidth; ++dx) {
            const size_t sx = (dx * srcWidth) / dstWidth;

            const size_t srcHw = sy * srcWidth + sx;

            const uint8_t r = srcPtr[0 * srcPlane + srcHw];
            const uint8_t g = srcPtr[1 * srcPlane + srcHw];
            const uint8_t b = srcPtr[2 * srcPlane + srcHw];

            const size_t dstIndex = (dy * Wdst + dx) * C; // HWC interleaved

            dstPtr[dstIndex + 0] = static_cast<Float16>(r * inv255);
            dstPtr[dstIndex + 1] = static_cast<Float16>(g * inv255);
            dstPtr[dstIndex + 2] = static_cast<Float16>(b * inv255);
        }
    }

    return true;
}

bool ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf32_Full_Chw(const ImageLayoutDesc &src,
                                                        const ImageLayoutDesc &dst,
                                                        Sampling sampling) {
    return StrechBlit_Rgb8_Chw_Rect_Rgbf32_Rect_Chw(src, dst, sampling);
}

bool ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf16_Full_Chw(const ImageLayoutDesc &src,
                                                        const ImageLayoutDesc &dst,
                                                        Sampling sampling) {
    const uint8_t *srcPtr = src.data;
    size_t srcWidth = src.surfaceWidth;
    size_t srcHeight = src.surfaceHeight;
    
    Float16 *dstPtr = (Float16 *)dst.data;
    size_t dstWidth = dst.surfaceWidth;
    size_t dstHeight = dst.surfaceHeight;
    
    if (!srcPtr || !dstPtr)
        return false;

    constexpr float inv255 = 1.0f / 255.0f;

    const size_t Wdst = dstWidth;
    const size_t Hdst = dstHeight;

    const size_t srcPlane = srcWidth * srcHeight; // CHW source planes
    const size_t dstPlane = Wdst * Hdst;          // CHW dest planes

    (void)sampling;

    // nearest-neighbor resize, CHW(u8) -> CHW(f16)
    for (size_t dy = 0; dy < dstHeight; ++dy) {
        const size_t sy = (dy * srcHeight) / dstHeight;

        for (size_t dx = 0; dx < dstWidth; ++dx) {
            const size_t sx = (dx * srcWidth) / dstWidth;

            const size_t srcHw = sy * srcWidth + sx;
            const size_t dstHw = dy * Wdst + dx;

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

bool ImageOps::StrechBlit_Rgb8_Chw_Rect_Rgbf32_Rect_Chw(const ImageLayoutDesc &src,
                                                        const ImageLayoutDesc &dst,
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

bool ImageOps::Clear_Rgbf32(
    float *dst, size_t dstWidth, size_t dstHeight, float r, float g, float b) {
    if (!dst)
        return false;

    const size_t plane = dstWidth * dstHeight;

    // CHW planar: [R plane][G plane][B plane]
    float *rPlane = dst + 0 * plane;
    float *gPlane = dst + 1 * plane;
    float *bPlane = dst + 2 * plane;

    for (size_t i = 0; i < plane; ++i) {
        rPlane[i] = r;
        gPlane[i] = g;
        bPlane[i] = b;
    }

    return true;
}

bool ImageOps::Fill_Rgbf32_Rect(
    float *dst, size_t dstWidth, size_t dstHeight, const PixelRect &dstRect, float r, float g, float b) {
    if (!dst)
        return false;

    if (dstRect.x + dstRect.width > dstWidth || dstRect.y + dstRect.height > dstHeight)
        return false;

    const size_t plane = dstWidth * dstHeight;

    float *rPlane = dst + 0 * plane;
    float *gPlane = dst + 1 * plane;
    float *bPlane = dst + 2 * plane;

    for (size_t dy = 0; dy < dstRect.height; ++dy) {
        const size_t y = dstRect.y + dy;
        const size_t rowBase = y * dstWidth;

        for (size_t dx = 0; dx < dstRect.width; ++dx) {
            const size_t x = dstRect.x + dx;
            const size_t hw = rowBase + x;

            rPlane[hw] = r;
            gPlane[hw] = g;
            bPlane[hw] = b;
        }
    }

    return true;
}

bool ImageOps::StrechBlit_Rgb8_Rect_Rgb8_Rect(const ImageLayoutDesc &src,
                                              const ImageLayoutDesc &dst,
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

    for (size_t dy = 0; dy < dstRect.height; ++dy) {
        const size_t sy = srcRect.y + (dy * srcRect.height) / dstRect.height;

        const uint8_t *srcRow = srcPtr + (sy * srcWidth + srcRect.x) * 3;
        uint8_t *dstRow = dstPtr + ((dstRect.y + dy) * dstWidth + dstRect.x) * 3;

        for (size_t dx = 0; dx < dstRect.width; ++dx) {
            const size_t sx = (dx * srcRect.width) / dstRect.width;

            const uint8_t *sp = srcRow + sx * 3;
            uint8_t *dp = dstRow + dx * 3;

            // Copy RGB
            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
        }
    }

    return true;
}

bool ImageOps::StrechBlit_Rgb8_Full_Rgb8_Full(const ImageLayoutDesc &src,
                                              const ImageLayoutDesc &dst,
                                              Sampling sampling) {
    return StrechBlit_Rgb8_Rect_Rgb8_Rect(src, dst, sampling);
}

bool ImageOps::Clear_Rgb8(
    uint8_t *dst, size_t dstWidth, size_t dstHeight, uint8_t r, uint8_t g, uint8_t b) {

    if (!dst)
        return false;

    uint8_t *p = dst;
    int count = dstWidth * dstHeight;
    for (int i = 0; i < count; i++) {
        p[3 * i + 0] = r;
        p[3 * i + 1] = g;
        p[3 * i + 2] = b;
    }

    return true;
}

bool ImageOps::Fill_Rgb8_Rect(uint8_t *dst,
                              size_t dstWidth,
                              size_t dstHeight,
                              const PixelRect &dstRect,
                              uint8_t r,
                              uint8_t g,
                              uint8_t b) {

    if (!dst)
        return false;

    if (dstRect.x + dstRect.width > dstWidth || dstRect.y + dstRect.height > dstHeight)
        return false;

    for (size_t dy = 0; dy < dstRect.height; ++dy) {
        const size_t dyi = dstRect.y + dy;
        uint8_t *row = dst + ((dyi * dstWidth) + dstRect.x) * 3;

        for (size_t dx = 0; dx < dstRect.width; ++dx) {
            uint8_t *px = row + dx * 3;
            px[0] = r; // R
            px[1] = g; // G
            px[2] = b; // B
        }
    }

    return true;
}
