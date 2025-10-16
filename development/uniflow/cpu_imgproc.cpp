#include "cpu_imgproc.h"

using namespace uflw;


void ImageConvert::Resize(uflw::ValuePointer src, size_t srcWidth, size_t srcHeight, RawDataLayout srcLayout,
    uflw::ValuePointer dst, size_t dstWidth, size_t dstHeight, RawDataLayout dstLayout) {

    if(srcLayout == RawDataLayout::Rgb8 && dstLayout == RawDataLayout::Rgbf32) {

        uflw::u8* in = (uflw::u8*)src;
        uflw::f32* out = (uflw::f32*)dst;

        for (size_t y = 0; y < dstHeight; ++y) {
            const size_t sy = y * srcHeight / dstHeight;
            for (size_t x = 0; x < dstWidth; ++x) {
                const size_t sx = x * srcWidth / dstWidth;
                const uflw::u8* p = in + (sy * srcWidth + sx) * 3;

                const size_t base = y * dstWidth + x;
                out[0 * dstHeight * dstWidth + base] = p[0] / 255.f;  // R
                out[1 * dstHeight * dstWidth + base] = p[1] / 255.f;  // G
                out[2 * dstHeight * dstWidth + base] = p[2] / 255.f;  // B
            }
        }

        return;
    }
        

}


