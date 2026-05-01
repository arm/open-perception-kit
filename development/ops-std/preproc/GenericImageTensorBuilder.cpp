/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#include "preproc/GenericImageTensorBuilder.h"
#include "preproc/CpuImageKernels.h"

#include <cassert>
#include <stdint.h>

using namespace amp;

amp::Result<void> amp::GenericImageTensorBuilder::build(const TensorBuilder::Setup &setup) {
    bool didBuild = false;

    if (setup.imageSourceDesc.kind == amp::DataKind::ImageBgraHwc &&
        setup.imageDestinationDesc.kind == amp::DataKind::ImageRgbChw) {
        if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
            setup.imageDestinationDesc.type == amp::Tdt::Float32) {
            amp::ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgbf32_Rect_Chw(
                setup.imageSourceDesc,
                setup.imageDestinationDesc);
            didBuild = true;
        } else if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
                   setup.imageDestinationDesc.type == amp::Tdt::Float16) {
            amp::ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgbf16_Rect_Chw(
                setup.imageSourceDesc,
                setup.imageDestinationDesc);
            didBuild = true;
        }
    }

    if (setup.imageSourceDesc.kind == amp::DataKind::ImageBgraHwc &&
        setup.imageDestinationDesc.kind == amp::DataKind::ImageRgbHwc) {
        if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
            setup.imageDestinationDesc.type == amp::Tdt::Uint8) {
            amp::ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgb8_Rect_Hwc(
                setup.imageSourceDesc,
                setup.imageDestinationDesc);
            didBuild = true;
        } else if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
                   setup.imageDestinationDesc.type == amp::Tdt::Float32) {
            amp::ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgbf32_Rect_Hwc(
                setup.imageSourceDesc,
                setup.imageDestinationDesc);
            didBuild = true;
        } else if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
                   setup.imageDestinationDesc.type == amp::Tdt::Float16) {
            amp::ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgbf16_Rect_Hwc(
                setup.imageSourceDesc,
                setup.imageDestinationDesc);
            didBuild = true;
        }
    }

    if (setup.imageSourceDesc.kind == amp::DataKind::ImageBgraHwc &&
        setup.imageDestinationDesc.kind == amp::DataKind::ImageGray) {
        if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
            setup.imageDestinationDesc.type == amp::Tdt::Uint8) {
            const bool isSrcFull = setup.imageSourceDesc.rect.x == 0 &&
                                   setup.imageSourceDesc.rect.y == 0 &&
                                   setup.imageSourceDesc.rect.width ==
                                       setup.imageSourceDesc.surfaceWidth &&
                                   setup.imageSourceDesc.rect.height ==
                                       setup.imageSourceDesc.surfaceHeight;
            const bool isDstFull = setup.imageDestinationDesc.rect.x == 0 &&
                                   setup.imageDestinationDesc.rect.y == 0 &&
                                   setup.imageDestinationDesc.rect.width ==
                                       setup.imageDestinationDesc.surfaceWidth &&
                                   setup.imageDestinationDesc.rect.height ==
                                       setup.imageDestinationDesc.surfaceHeight;

            if (isSrcFull && isDstFull) {
                amp::ImageOps::StretchBlit_Bgra8_Hwc_Full_Gray8_Full(setup.imageSourceDesc,
                                                                     setup.imageDestinationDesc);
            } else {
                amp::ImageOps::StretchBlit_Bgra8_Hwc_Rect_Gray8_Rect(setup.imageSourceDesc,
                                                                     setup.imageDestinationDesc);
            }
            didBuild = true;
        }
    }

    if (setup.imageSourceDesc.kind == amp::DataKind::ImageRgbChw &&
        setup.imageDestinationDesc.kind == amp::DataKind::ImageRgbChw) {
        if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
            setup.imageDestinationDesc.type == amp::Tdt::Float32) {
            amp::ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf32_Full_Chw(
                setup.imageSourceDesc,
                setup.imageDestinationDesc);
            didBuild = true;
        } else if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
                   setup.imageDestinationDesc.type == amp::Tdt::Float16) {
            amp::ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf16_Full_Chw(
                setup.imageSourceDesc,
                setup.imageDestinationDesc);
            didBuild = true;
        }
    }

    if (setup.imageSourceDesc.kind == amp::DataKind::ImageRgbChw &&
        setup.imageDestinationDesc.kind == amp::DataKind::ImageRgbHwc) {
        if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
            setup.imageDestinationDesc.type == amp::Tdt::Float32) {
            amp::ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf32_Full_Hwc(
                setup.imageSourceDesc,
                setup.imageDestinationDesc);
            didBuild = true;
        } else if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
                   setup.imageDestinationDesc.type == amp::Tdt::Float16) {
            amp::ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf16_Full_Hwc(
                setup.imageSourceDesc,
                setup.imageDestinationDesc);
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
