#include "cpu_kernels.h"

using namespace uflw;

bool ImageOps::StrechBlit_Rgb8_Rect_Rgbf32_Rect(
    uint8_t* src, size_t srcWidth, size_t srcHeight, const ImageOps::Rect& srcRect,
    uint8_t* dst, size_t dstWidth, size_t dstHeight, const ImageOps::Rect& dstRect,
    Sampling sampling)
{
    if(!src || !dst) return false;

    if(srcRect.x + srcRect.w > srcWidth || srcRect.y + srcRect.h > srcHeight ||
        dstRect.x + dstRect.w > dstWidth || dstRect.y + dstRect.h > dstHeight)
        return false;

    const uint8_t* in = src;
    float* out = (float*)dst;
    constexpr float inv255 = 1.0f / 255.0f;

    // nearest-neighbor sampling stretch from srcRect to dstRect, packed RGBf32 on dst
    for (size_t dy = 0; dy < dstRect.h; ++dy) {
        const size_t sy = srcRect.y + (dy * srcRect.h) / dstRect.h;
        const size_t dyi = dstRect.y + dy;

        for (size_t dx = 0; dx < dstRect.w; ++dx) {
            const size_t sx = srcRect.x + (dx * srcRect.w) / dstRect.w;

            const size_t srcIndexRGB = (sy * srcWidth + sx) * 3;
            const uint8_t* p = in + srcIndexRGB;

            const size_t dxi = dstRect.x + dx;
            const size_t dstIndexRGB = (dyi * dstWidth + dxi) * 3;

            out[dstIndexRGB + 0] = p[0] * inv255; // R
            out[dstIndexRGB + 1] = p[1] * inv255; // G
            out[dstIndexRGB + 2] = p[2] * inv255; // B
        }
    }

    return true;
}

bool ImageOps::StrechBlit_Rgb8_Full_Rgbf32_Full(uint8_t* src, size_t srcWidth, size_t srcHeight, 
    uint8_t* dst, size_t dstWidth, size_t dstHeight, Sampling sampling) {
    Rect srcRect = { 0, 0, srcWidth, srcHeight };
    Rect dstRect = { 0, 0, dstWidth, dstHeight };
    return StrechBlit_Rgb8_Rect_Rgbf32_Rect(src, srcWidth, srcHeight, srcRect, 
        dst, dstWidth, dstHeight, dstRect, sampling);
}

bool ImageOps::Clear_Rgbf32(uint8_t* dst, size_t dstWidth, size_t dstHeight, float r, float g, float b) {

    if(!dst) return false;

    float* p = (float*)dst;
    int count = dstWidth * dstHeight;
    for(int i = 0; i < count; i++) {
        p[3 * i + 0] = r;
        p[3 * i + 1] = g;
        p[3 * i + 2] = b;
    }

    return true;
}

bool ImageOps::Fill_Rgbf32_Rect(uint8_t* dst, size_t dstWidth, size_t dstHeight, const Rect& dstRect, float r, float g, float b) {

    if (!dst) return false;

    if (dstRect.x + dstRect.w > dstWidth || dstRect.y + dstRect.h > dstHeight)
        return false;

    float* out = (float*)dst;
    const size_t planeStride = dstWidth * dstHeight;

    for (size_t dy = 0; dy < dstRect.h; ++dy) {
        const size_t dyi = dstRect.y + dy;
        const size_t rowBase = dyi * dstWidth + dstRect.x;

        for (size_t dx = 0; dx < dstRect.w; ++dx) {
            const size_t base = rowBase + dx;

            out[0 * planeStride + base] = r;
            out[1 * planeStride + base] = g;
            out[2 * planeStride + base] = b;
        }
    }

    return true;
}


