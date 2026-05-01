/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/

#pragma once

#include "amp/TensorBuilder.h"
#include "amp/Types.h"

namespace amp {

struct ImageOps {

    // ---
    static bool
    StretchBlit_Bgra8_Hwc_Full_Rgbf32_Full_Chw(const ImageLayoutDesc &src,
                                               const ImageLayoutDesc &dst,
                                               Sampling sampling = Sampling::Nearest);

    static bool
    StretchBlit_Bgra8_Hwc_Rect_Rgbf32_Rect_Chw(const ImageLayoutDesc &src,
                                               const ImageLayoutDesc &dst,
                                               Sampling sampling = Sampling::Nearest);

    static bool StretchBlit_Bgra8_Hwc_Rect_Rgb8_Rect_Hwc(const ImageLayoutDesc &src,
                                                         const ImageLayoutDesc &dst,
                                                         Sampling sampling = Sampling::Nearest);

    static bool
    StretchBlit_Bgra8_Hwc_Rect_Rgbf32_Rect_Hwc(const ImageLayoutDesc &src,
                                               const ImageLayoutDesc &dst,
                                               Sampling sampling = Sampling::Nearest);

    static bool
    StretchBlit_Bgra8_Hwc_Rect_Rgbf16_Rect_Hwc(const ImageLayoutDesc &src,
                                               const ImageLayoutDesc &dst,
                                               Sampling sampling = Sampling::Nearest);

    static bool
    StretchBlit_Bgra8_Hwc_Rect_Rgbf16_Rect_Chw(const ImageLayoutDesc &src,
                                               const ImageLayoutDesc &dst,
                                               Sampling sampling = Sampling::Nearest);

    static bool
    StretchBlit_Bgra8_Hwc_Full_Gray8_Full(const ImageLayoutDesc &src,
                                          const ImageLayoutDesc &dst,
                                          Sampling sampling = Sampling::Nearest);

    static bool
    StretchBlit_Bgra8_Hwc_Rect_Gray8_Rect(const ImageLayoutDesc &src,
                                          const ImageLayoutDesc &dst,
                                          Sampling sampling = Sampling::Nearest);

    static bool
    StrechBlit_Rgb8_Chw_Rect_Rgbf32_Rect_Hwc(const ImageLayoutDesc &src,
                                             const ImageLayoutDesc &dst,
                                             Sampling sampling = Sampling::Nearest);

    static bool
    StrechBlit_Rgb8_Chw_Full_Rgbf32_Full_Hwc(const ImageLayoutDesc &src,
                                             const ImageLayoutDesc &dst,
                                             Sampling sampling = Sampling::Nearest);

    static bool
    StrechBlit_Rgb8_Chw_Full_Rgbf16_Full_Hwc(const ImageLayoutDesc &src,
                                             const ImageLayoutDesc &dst,
                                             Sampling sampling = Sampling::Nearest);

    static bool
    StrechBlit_Rgb8_Chw_Rect_Rgbf32_Rect_Chw(const ImageLayoutDesc &src,
                                             const ImageLayoutDesc &dst,
                                             Sampling sampling = Sampling::Nearest);

    static bool
    StrechBlit_Rgb8_Chw_Full_Rgbf32_Full_Chw(const ImageLayoutDesc &src,
                                             const ImageLayoutDesc &dst,
                                             Sampling sampling = Sampling::Nearest);

    static bool
    StrechBlit_Rgb8_Chw_Full_Rgbf16_Full_Chw(const ImageLayoutDesc &src,
                                             const ImageLayoutDesc &dst,
                                             Sampling sampling = Sampling::Nearest);

    static bool
    Clear_Rgbf32(float *dst, size_t dstWidth, size_t dstHeight, float r, float g, float b);

    static bool Fill_Rgbf32_Rect(float *dst,
                                 size_t dstWidth,
                                 size_t dstHeight,
                                 const PixelRect &dstRect,
                                 float r,
                                 float g,
                                 float b);

    // ---

    static bool StrechBlit_Rgb8_Rect_Rgb8_Rect(const ImageLayoutDesc &src,
                                               const ImageLayoutDesc &dst,
                                               Sampling sampling = Sampling::Nearest);

    static bool StrechBlit_Rgb8_Full_Rgb8_Full(const ImageLayoutDesc &src,
                                               const ImageLayoutDesc &dst,
                                               Sampling sampling = Sampling::Nearest);

    static bool
    Clear_Rgb8(uint8_t *dst, size_t dstWidth, size_t dstHeight, uint8_t r, uint8_t g, uint8_t b);

    static bool Fill_Rgb8_Rect(uint8_t *dst,
                               size_t dstWidth,
                               size_t dstHeight,
                               const PixelRect &dstRect,
                               uint8_t r,
                               uint8_t g,
                               uint8_t b);
};

} // namespace amp
