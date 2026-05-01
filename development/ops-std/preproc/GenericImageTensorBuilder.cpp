/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#include "preproc/GenericImageTensorBuilder.h"
#include "preproc/CpuImageKernels.h"

#include <stdint.h>

using namespace amp;

amp::Result<void> amp::GenericImageTensorBuilder::build(const TensorBuilder::Setup &setup) {

    const uint8_t *src = setup.imageSourceDesc.data;
    size_t srcX = setup.imageSourceDesc.x;
    size_t srcY = setup.imageSourceDesc.y;
    size_t srcWidth = setup.imageSourceDesc.width;
    size_t srcHeight = setup.imageSourceDesc.height;
    size_t srcByteCount = setup.imageSourceDesc.byteCount;
    size_t srcFullWidth = setup.imageSourceDesc.surfaceWidth;
    size_t srcFullHeight = setup.imageSourceDesc.surfaceHeight;

    uint8_t *dst = setup.imageDestinationDesc.data;
    size_t dstX = setup.imageDestinationDesc.x;
    size_t dstY = setup.imageDestinationDesc.y;
    size_t dstWidth = setup.imageDestinationDesc.width;
    size_t dstHeight = setup.imageDestinationDesc.height;
    size_t dstByteCount = setup.imageDestinationDesc.byteCount;
    size_t dstFullWidth = setup.imageDestinationDesc.surfaceWidth;
    size_t dstFullHeight = setup.imageDestinationDesc.surfaceHeight;

    bool didBuild = false;

    if (setup.imageSourceDesc.kind == amp::DataKind::ImageBgraHwc &&
        setup.imageDestinationDesc.kind == amp::DataKind::ImageRgbChw) {
        if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
            setup.imageDestinationDesc.type == amp::Tdt::Float32) {

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
                                                                      setup.imageSourceDesc.mean,
                                                                      setup.imageSourceDesc.std);
            didBuild = true;
        } else if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
                   setup.imageDestinationDesc.type == amp::Tdt::Float16) {
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
                                                                      setup.imageSourceDesc.mean,
                                                                      setup.imageSourceDesc.std);
            didBuild = true;
        }
    }

    if (setup.imageSourceDesc.kind == amp::DataKind::ImageBgraHwc &&
        setup.imageDestinationDesc.kind == amp::DataKind::ImageRgbHwc) {
        if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
            setup.imageDestinationDesc.type == amp::Tdt::Uint8) {
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
        } else if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
                   setup.imageDestinationDesc.type == amp::Tdt::Float32) {
            amp::ImageOps::Rect srcRect(srcX, srcY, srcWidth, srcHeight);
            amp::ImageOps::Rect dstRect(dstX, dstY, dstWidth, dstHeight);
            amp::ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgbf32_Rect_Hwc(src,
                                                                       srcFullWidth,
                                                                       srcFullHeight,
                                                                       srcRect,
                                                                       (float *)dst,
                                                                       dstFullWidth,
                                                                       dstFullHeight,
                                                                       dstRect,
                                                                       setup.imageSourceDesc.mean,
                                                                       setup.imageSourceDesc.std);
            didBuild = true;
        } else if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
                   setup.imageDestinationDesc.type == amp::Tdt::Float16) {
            amp::ImageOps::Rect srcRect(srcX, srcY, srcWidth, srcHeight);
            amp::ImageOps::Rect dstRect(dstX, dstY, dstWidth, dstHeight);
            amp::ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgbf16_Rect_Hwc(src,
                                                                       srcFullWidth,
                                                                       srcFullHeight,
                                                                       srcRect,
                                                                       (amp::Float16 *)dst,
                                                                       dstFullWidth,
                                                                       dstFullHeight,
                                                                       dstRect,
                                                                       setup.imageSourceDesc.mean,
                                                                       setup.imageSourceDesc.std);
            didBuild = true;
        }
    }

    if (setup.imageSourceDesc.kind == amp::DataKind::ImageRgbChw &&
        setup.imageDestinationDesc.kind == amp::DataKind::ImageRgbChw) {

        if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
            setup.imageDestinationDesc.type == amp::Tdt::Float32) {
            amp::ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf32_Full_Chw(src,
                                                                    srcWidth,
                                                                    srcHeight,
                                                                    (float *)dst,
                                                                    dstWidth,
                                                                    dstHeight,
                                                                    setup.imageSourceDesc.mean,
                                                                    setup.imageSourceDesc.std);
            didBuild = true;
        } else if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
                   setup.imageDestinationDesc.type == amp::Tdt::Float16) {
            amp::ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf16_Full_Chw(src,
                                                                    srcWidth,
                                                                    srcHeight,
                                                                    (amp::Float16 *)dst,
                                                                    dstWidth,
                                                                    dstHeight,
                                                                    setup.imageSourceDesc.mean,
                                                                    setup.imageSourceDesc.std);
            didBuild = true;
        }
    }

    if (setup.imageSourceDesc.kind == amp::DataKind::ImageRgbChw &&
        setup.imageDestinationDesc.kind == amp::DataKind::ImageRgbHwc) {

        if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
            setup.imageDestinationDesc.type == amp::Tdt::Float32) {
            amp::ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf32_Full_Hwc(src,
                                                                    srcWidth,
                                                                    srcHeight,
                                                                    (float *)dst,
                                                                    dstWidth,
                                                                    dstHeight,
                                                                    setup.imageSourceDesc.mean,
                                                                    setup.imageSourceDesc.std);
            didBuild = true;
        } else if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
                   setup.imageDestinationDesc.type == amp::Tdt::Float16) {
            amp::ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf16_Full_Hwc(src,
                                                                    srcWidth,
                                                                    srcHeight,
                                                                    (amp::Float16 *)dst,
                                                                    dstWidth,
                                                                    dstHeight,
                                                                    setup.imageSourceDesc.mean,
                                                                    setup.imageSourceDesc.std);
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
