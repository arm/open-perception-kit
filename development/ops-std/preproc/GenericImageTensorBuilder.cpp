/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#include "preproc/GenericImageTensorBuilder.h"
#include "preproc/CpuImageKernels.h"

#include <stdint.h>

using namespace amp;

amp::Result<void> amp::GenericImageTensorBuilder::build(const TensorBuilder::Setup &setup) {

    const uint8_t *src = setup.imageSource.data;
    size_t srcX = setup.imageSource.x;
    size_t srcY = setup.imageSource.y;
    size_t srcWidth = setup.imageSource.width;
    size_t srcHeight = setup.imageSource.height;
    size_t srcByteCount = setup.imageSource.byteCount;
    size_t srcFullWidth = setup.imageSource.surfaceWidth;
    size_t srcFullHeight = setup.imageSource.surfaceHeight;

    uint8_t *dst = setup.imageDestination.data;
    size_t dstX = setup.imageDestination.x;
    size_t dstY = setup.imageDestination.y;
    size_t dstWidth = setup.imageDestination.width;
    size_t dstHeight = setup.imageDestination.height;
    size_t dstByteCount = setup.imageDestination.byteCount;
    size_t dstFullWidth = setup.imageDestination.surfaceWidth;
    size_t dstFullHeight = setup.imageDestination.surfaceHeight;

    bool didBuild = false;

    if (setup.imageSource.kind == amp::DataKind::ImageBgraHwc &&
        setup.imageDestination.kind == amp::DataKind::ImageRgbChw) {
        if (setup.imageSource.type == amp::Tdt::Uint8 &&
            setup.imageDestination.type == amp::Tdt::Float32) {

            //                amp::ImageOps::StretchBlit_Bgra8_Hwc_Full_Rgbf32_Full_Chw(
            //                src, srcWidth, srcHeight, (float *)dst, dstWidth, dstHeight);
            amp::ImageOps::Rect srcRect(srcX, srcY, srcWidth, srcHeight);
            amp::ImageOps::Rect dstRect(dstX, dstY, dstWidth, dstHeight);
            amp::ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgbf32_Rect_Chw(src,
                                                                      srcFullWidth,
                                                                      srcFullHeight,
                                                                      srcRect,
                                                                      (float *)dst,
                                                                      dstFullWidth,
                                                                      dstFullHeight,
                                                                      dstRect,
                                                                      setup.imageSource.mean,
                                                                      setup.imageSource.std);
            didBuild = true;
        } else if (setup.imageSource.type == amp::Tdt::Uint8 &&
                   setup.imageDestination.type == amp::Tdt::Float16) {
            amp::ImageOps::Rect srcRect(srcX, srcY, srcWidth, srcHeight);
            amp::ImageOps::Rect dstRect(dstX, dstY, dstWidth, dstHeight);
            amp::ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgbf16_Rect_Chw(src,
                                                                      srcFullWidth,
                                                                      srcFullHeight,
                                                                      srcRect,
                                                                      (amp::Float16 *)dst,
                                                                      dstFullWidth,
                                                                      dstFullHeight,
                                                                      dstRect,
                                                                      setup.imageSource.mean,
                                                                      setup.imageSource.std);
            didBuild = true;
        }
    }

    if (setup.imageSource.kind == amp::DataKind::ImageBgraHwc &&
        setup.imageDestination.kind == amp::DataKind::ImageRgbHwc) {
        if (setup.imageSource.type == amp::Tdt::Uint8 &&
            setup.imageDestination.type == amp::Tdt::Uint8) {
            amp::ImageOps::Rect srcRect(srcX, srcY, srcWidth, srcHeight);
            amp::ImageOps::Rect dstRect(dstX, dstY, dstWidth, dstHeight);
            amp::ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgb8_Rect_Hwc(src,
                                                                    srcFullWidth,
                                                                    srcFullHeight,
                                                                    srcRect,
                                                                    dst,
                                                                    dstFullWidth,
                                                                    dstFullHeight,
                                                                    dstRect);
            didBuild = true;
        }
    }

    if (setup.imageSource.kind == amp::DataKind::ImageRgbChw &&
        setup.imageDestination.kind == amp::DataKind::ImageRgbChw) {

        if (setup.imageSource.type == amp::Tdt::Uint8 &&
            setup.imageDestination.type == amp::Tdt::Float32) {
            amp::ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf32_Full_Chw(src,
                                                                    srcWidth,
                                                                    srcHeight,
                                                                    (float *)dst,
                                                                    dstWidth,
                                                                    dstHeight,
                                                                    setup.imageSource.mean,
                                                                    setup.imageSource.std);
            didBuild = true;
        } else if (setup.imageSource.type == amp::Tdt::Uint8 &&
                   setup.imageDestination.type == amp::Tdt::Float16) {
            amp::ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf16_Full_Chw(src,
                                                                    srcWidth,
                                                                    srcHeight,
                                                                    (amp::Float16 *)dst,
                                                                    dstWidth,
                                                                    dstHeight,
                                                                    setup.imageSource.mean,
                                                                    setup.imageSource.std);
            didBuild = true;
        }
    }

    if (setup.imageSource.kind == amp::DataKind::ImageRgbChw &&
        setup.imageDestination.kind == amp::DataKind::ImageRgbHwc) {

        if (setup.imageSource.type == amp::Tdt::Uint8 &&
            setup.imageDestination.type == amp::Tdt::Float32) {
            amp::ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf32_Full_Hwc(src,
                                                                    srcWidth,
                                                                    srcHeight,
                                                                    (float *)dst,
                                                                    dstWidth,
                                                                    dstHeight,
                                                                    setup.imageSource.mean,
                                                                    setup.imageSource.std);
            didBuild = true;
        } else if (setup.imageSource.type == amp::Tdt::Uint8 &&
                   setup.imageDestination.type == amp::Tdt::Float16) {
            amp::ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf16_Full_Hwc(src,
                                                                    srcWidth,
                                                                    srcHeight,
                                                                    (amp::Float16 *)dst,
                                                                    dstWidth,
                                                                    dstHeight,
                                                                    setup.imageSource.mean,
                                                                    setup.imageSource.std);
            didBuild = true;
        }
    }

    if (!didBuild) {
        return tl::unexpected(AMP_ERROR(
            amp::ErrorFlag::InvalidData,
            "GenericImageTensorBuilder: unsupported source/destination kind+type conversion"));
    }

    return {};
}
