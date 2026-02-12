#pragma once

#include "amp/Types.h"

namespace amp {

struct ImageOps {

    enum Sampling { Nearest, Linear };

    struct Rect {
        Rect(size_t x, size_t y, size_t w, size_t h) {
            this->x = x;
            this->y = y;
            this->w = w;
            this->h = h;
        }

        size_t x, y, w, h;
    };

    // ---
    static bool
    StretchBlit_Bgra8_Hwc_Full_Rgbf32_Full_Chw(const uint8_t *src,
                                               size_t srcWidth,
                                               size_t srcHeight,
                                               float *dst,
                                               size_t dstWidth,
                                               size_t dstHeight,
                                               const Colorf &mean = {0.0f, 0.0f, 0.0f, 0.0f},
                                               const Colorf &std = {1.0f, 1.0f, 1.0f, 1.0f},
                                               Sampling sampling = Sampling::Nearest);

    static bool
    StretchBlit_Bgra8_Hwc_Rect_Rgbf32_Rect_Chw(const uint8_t *src,
                                               size_t srcWidth,
                                               size_t srcHeight,
                                               const Rect &srcRect,
                                               float *dst,
                                               size_t dstWidth,
                                               size_t dstHeight,
                                               const Rect &dstRect,
                                               const Colorf &mean = {0.0f, 0.0f, 0.0f, 0.0f},
                                               const Colorf &std = {1.0f, 1.0f, 1.0f, 1.0f},
                                               Sampling sampling = Sampling::Nearest);

    static bool
    StrechBlit_Rgb8_Chw_Rect_Rgbf32_Rect_Hwc(const uint8_t *src,
                                             size_t srcWidth,
                                             size_t srcHeight,
                                             const Rect &srcRect,
                                             float *dst,
                                             size_t dstWidth,
                                             size_t dstHeight,
                                             const Rect &dstRect,
                                             const Colorf &mean = {0.0f, 0.0f, 0.0f, 0.0f},
                                             const Colorf &std = {1.0f, 1.0f, 1.0f, 1.0f},

                                             Sampling sampling = Sampling::Nearest);

    static bool
    StrechBlit_Rgb8_Chw_Full_Rgbf32_Full_Hwc(const uint8_t *src,
                                             size_t srcWidth,
                                             size_t srcHeight,
                                             float *dst,
                                             size_t dstWidth,
                                             size_t dstHeight,
                                             const Colorf &mean = {0.0f, 0.0f, 0.0f, 0.0f},
                                             const Colorf &std = {1.0f, 1.0f, 1.0f, 1.0f},

                                             Sampling sampling = Sampling::Nearest);

    static bool
    StrechBlit_Rgb8_Chw_Rect_Rgbf32_Rect_Chw(const uint8_t *src,
                                             size_t srcWidth,
                                             size_t srcHeight,
                                             const Rect &srcRect,
                                             float *dst,
                                             size_t dstWidth,
                                             size_t dstHeight,
                                             const Rect &dstRect,
                                             const Colorf &mean = {0.0f, 0.0f, 0.0f, 0.0f},
                                             const Colorf &std = {1.0f, 1.0f, 1.0f, 1.0f},

                                             Sampling sampling = Sampling::Nearest);

    static bool
    StrechBlit_Rgb8_Chw_Full_Rgbf32_Full_Chw(const uint8_t *src,
                                             size_t srcWidth,
                                             size_t srcHeight,
                                             float *dst,
                                             size_t dstWidth,
                                             size_t dstHeight,
                                             const Colorf &mean = {0.0f, 0.0f, 0.0f, 0.0f},
                                             const Colorf &std = {1.0f, 1.0f, 1.0f, 1.0f},
                                             Sampling sampling = Sampling::Nearest);

    static bool
    Clear_Rgbf32(float *dst, size_t dstWidth, size_t dstHeight, float r, float g, float b);

    static bool Fill_Rgbf32_Rect(float *dst,
                                 size_t dstWidth,
                                 size_t dstHeight,
                                 const Rect &dstRect,
                                 float r,
                                 float g,
                                 float b);

    // ---

    static bool StrechBlit_Rgb8_Rect_Rgb8_Rect(const uint8_t *src,
                                               size_t srcWidth,
                                               size_t srcHeight,
                                               const Rect &srcRect,
                                               uint8_t *dst,
                                               size_t dstWidth,
                                               size_t dstHeight,
                                               const Rect &dstRect,
                                               const Colorf &mean = {0.0f, 0.0f, 0.0f, 0.0f},
                                               const Colorf &std = {1.0f, 1.0f, 1.0f, 1.0f},
                                               Sampling sampling = Sampling::Nearest);

    static bool StrechBlit_Rgb8_Full_Rgb8_Full(const uint8_t *src,
                                               size_t srcWidth,
                                               size_t srcHeight,
                                               uint8_t *dst,
                                               size_t dstWidth,
                                               size_t dstHeight,
                                               const Colorf &mean = {0.0f, 0.0f, 0.0f, 0.0f},
                                               const Colorf &std = {1.0f, 1.0f, 1.0f, 1.0f},
                                               Sampling sampling = Sampling::Nearest);

    static bool
    Clear_Rgb8(uint8_t *dst, size_t dstWidth, size_t dstHeight, uint8_t r, uint8_t g, uint8_t b);

    static bool Fill_Rgb8_Rect(uint8_t *dst,
                               size_t dstWidth,
                               size_t dstHeight,
                               const Rect &dstRect,
                               uint8_t r,
                               uint8_t g,
                               uint8_t b);
};

} // namespace amp
