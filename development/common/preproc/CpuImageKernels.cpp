#include "preproc/CpuImageKernels.h"

using namespace amp;

bool ImageOps::StrechBlit_Rgb8_Chw_Rect_Rgbf32_Rect_Hwc(const uint8_t *src,
                                                        size_t srcWidth,
                                                        size_t srcHeight,
                                                        const ImageOps::Rect &srcRect,
                                                        float *dst,
                                                        size_t dstWidth,
                                                        size_t dstHeight,
                                                        const ImageOps::Rect &dstRect,
                                                        Sampling sampling) {
    if (!src || !dst)
        return false;

    if (srcRect.x + srcRect.w > srcWidth || srcRect.y + srcRect.h > srcHeight ||
        dstRect.x + dstRect.w > dstWidth || dstRect.y + dstRect.h > dstHeight)
        return false;

    const uint8_t *in = src; // source HWC u8
    float *out = dst;        // destination HWC float32
    constexpr float inv255 = 1.0f / 255.0f;

    const size_t C = 3;
    const size_t H = dstHeight;
    const size_t W = dstWidth;

    // nearest-neighbor resize, HWC → HWC
    for (size_t dy = 0; dy < dstRect.h; ++dy) {
        const size_t sy = srcRect.y + (dy * srcRect.h) / dstRect.h;
        const size_t dyi = dstRect.y + dy;

        for (size_t dx = 0; dx < dstRect.w; ++dx) {
            const size_t sx = srcRect.x + (dx * srcRect.w) / dstRect.w;
            const size_t dxi = dstRect.x + dx;

            // src (u8 HWC interleaved)
            const size_t srcIndexRGB = (sy * srcWidth + sx) * C;
            const uint8_t *p = in + srcIndexRGB;

            // dst (float32 HWC interleaved)
            const size_t dstIndexRGB = (dyi * W + dxi) * C;

            out[dstIndexRGB + 0] = p[0] * inv255; // R
            out[dstIndexRGB + 1] = p[1] * inv255; // G
            out[dstIndexRGB + 2] = p[2] * inv255; // B
        }
    }

    return true;
}

bool ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf32_Full_Hwc(const uint8_t *src,
                                                        size_t srcWidth,
                                                        size_t srcHeight,
                                                        float *dst,
                                                        size_t dstWidth,
                                                        size_t dstHeight,
                                                        Sampling sampling) {
    Rect srcRect = {0, 0, srcWidth, srcHeight};
    Rect dstRect = {0, 0, dstWidth, dstHeight};
    return StrechBlit_Rgb8_Chw_Rect_Rgbf32_Rect_Hwc(
        src, srcWidth, srcHeight, srcRect, dst, dstWidth, dstHeight, dstRect, sampling);
}

bool ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf32_Full_Chw(const uint8_t *src,
                                                        size_t srcWidth,
                                                        size_t srcHeight,
                                                        float *dst,
                                                        size_t dstWidth,
                                                        size_t dstHeight,
                                                        Sampling sampling) {
    Rect srcRect = {0, 0, srcWidth, srcHeight};
    Rect dstRect = {0, 0, dstWidth, dstHeight};
    return StrechBlit_Rgb8_Chw_Rect_Rgbf32_Rect_Chw(
        src, srcWidth, srcHeight, srcRect, dst, dstWidth, dstHeight, dstRect, sampling);
}

bool ImageOps::StrechBlit_Rgb8_Chw_Rect_Rgbf32_Rect_Chw(const uint8_t *src,
                                                        size_t srcWidth,
                                                        size_t srcHeight,
                                                        const ImageOps::Rect &srcRect,
                                                        float *dst,
                                                        size_t dstWidth,
                                                        size_t dstHeight,
                                                        const ImageOps::Rect &dstRect,
                                                        Sampling sampling) {
    if (!src || !dst)
        return false;

    if (srcRect.x + srcRect.w > srcWidth || srcRect.y + srcRect.h > srcHeight ||
        dstRect.x + dstRect.w > dstWidth || dstRect.y + dstRect.h > dstHeight)
        return false;

    const uint8_t *in = src;
    float *out = dst;
    constexpr float inv255 = 1.0f / 255.0f;

    const size_t C = 3;
    const size_t H = dstHeight;
    const size_t W = dstWidth;

    const size_t planeSize = H * W;

    // nearest-neighbour sampling stretch from srcRect to dstRect, CHW layout
    for (size_t dy = 0; dy < dstRect.h; ++dy) {
        const size_t sy = srcRect.y + (dy * srcRect.h) / dstRect.h;
        const size_t dyi = dstRect.y + dy;

        for (size_t dx = 0; dx < dstRect.w; ++dx) {
            const size_t sx = srcRect.x + (dx * srcRect.w) / dstRect.w;
            const size_t dxi = dstRect.x + dx;

            // source: RGB interleaved, HWC
            const size_t srcIndexRGB = (sy * srcWidth + sx) * 3;
            const uint8_t *p = in + srcIndexRGB;

            // CHW destination
            const size_t hwIndex = dyi * W + dxi;

            out[0 * planeSize + hwIndex] = ((p[0] * inv255) - 0.485f) / 0.229f;
            out[1 * planeSize + hwIndex] = ((p[1] * inv255) - 0.456f) / 0.224f;
            out[2 * planeSize + hwIndex] = ((p[2] * inv255) - 0.406f) / 0.225f;
        }
    }

    return true;
}

bool ImageOps::Clear_Rgbf32(
    float *dst, size_t dstWidth, size_t dstHeight, float r, float g, float b) {

    if (!dst)
        return false;

    float *p = dst;
    int count = dstWidth * dstHeight;
    for (int i = 0; i < count; i++) {
        p[3 * i + 0] = r;
        p[3 * i + 1] = g;
        p[3 * i + 2] = b;
    }

    return true;
}

bool ImageOps::Fill_Rgbf32_Rect(
    float *dst, size_t dstWidth, size_t dstHeight, const Rect &dstRect, float r, float g, float b) {

    if (!dst)
        return false;

    if (dstRect.x + dstRect.w > dstWidth || dstRect.y + dstRect.h > dstHeight)
        return false;

    for (size_t dy = 0; dy < dstRect.h; ++dy) {
        const size_t dyi = dstRect.y + dy;
        float *row = dst + (dyi * dstWidth + dstRect.x) * 3; // 3 floats per pixel

        for (size_t dx = 0; dx < dstRect.w; ++dx) {
            float *px = row + dx * 3;
            px[0] = r;
            px[1] = g;
            px[2] = b;
        }
    }

    return true;
}

bool ImageOps::StrechBlit_Rgb8_Rect_Rgb8_Rect(const uint8_t *src,
                                              size_t srcWidth,
                                              size_t srcHeight,
                                              const Rect &srcRect,
                                              uint8_t *dst,
                                              size_t dstWidth,
                                              size_t dstHeight,
                                              const Rect &dstRect,
                                              Sampling sampling) {
    if (!src || !dst)
        return false;

    if (srcRect.x + srcRect.w > srcWidth || srcRect.y + srcRect.h > srcHeight ||
        dstRect.x + dstRect.w > dstWidth || dstRect.y + dstRect.h > dstHeight)
        return false;

    for (size_t dy = 0; dy < dstRect.h; ++dy) {
        const size_t sy = srcRect.y + (dy * srcRect.h) / dstRect.h;

        const uint8_t *srcRow = src + (sy * srcWidth + srcRect.x) * 3;
        uint8_t *dstRow = dst + ((dstRect.y + dy) * dstWidth + dstRect.x) * 3;

        for (size_t dx = 0; dx < dstRect.w; ++dx) {
            const size_t sx = (dx * srcRect.w) / dstRect.w;

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

bool ImageOps::StrechBlit_Rgb8_Full_Rgb8_Full(const uint8_t *src,
                                              size_t srcWidth,
                                              size_t srcHeight,
                                              uint8_t *dst,
                                              size_t dstWidth,
                                              size_t dstHeight,
                                              Sampling sampling) {
    Rect srcRect = {0, 0, srcWidth, srcHeight};
    Rect dstRect = {0, 0, dstWidth, dstHeight};
    return StrechBlit_Rgb8_Rect_Rgb8_Rect(
        src, srcWidth, srcHeight, srcRect, dst, dstWidth, dstHeight, dstRect, sampling);
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
                              const Rect &dstRect,
                              uint8_t r,
                              uint8_t g,
                              uint8_t b) {

    if (!dst)
        return false;

    if (dstRect.x + dstRect.w > dstWidth || dstRect.y + dstRect.h > dstHeight)
        return false;

    for (size_t dy = 0; dy < dstRect.h; ++dy) {
        const size_t dyi = dstRect.y + dy;
        uint8_t *row = dst + ((dyi * dstWidth) + dstRect.x) * 3;

        for (size_t dx = 0; dx < dstRect.w; ++dx) {
            uint8_t *px = row + dx * 3;
            px[0] = r; // R
            px[1] = g; // G
            px[2] = b; // B
        }
    }

    return true;
}
