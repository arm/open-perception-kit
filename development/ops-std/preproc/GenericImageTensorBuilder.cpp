/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#include "preproc/GenericImageTensorBuilder.h"
#include "preproc/CpuImageKernels.h"

#include <cassert>
#include <stdint.h>

using namespace amp;

namespace {

bool canUseFullKernelFastPath(const amp::ImageLayoutDesc &src, const amp::ImageLayoutDesc &dst) {
    const bool isSrcFull = src.rectIsFullSurface();
    const bool isDstFull = dst.rectIsFullSurface();
    const bool sameSize =
        (src.surfaceWidth == dst.surfaceWidth) && (src.surfaceHeight == dst.surfaceHeight);
    return isSrcFull && isDstFull && sameSize;
}

} // namespace

amp::Result<void> amp::GenericImageTensorBuilder::build(const TensorBuilder::Setup &setup) {
    bool didBuild = false;

    if (setup.imageSourceDesc.kind == amp::DataKind::ImageBgraHwc &&
        setup.imageDestinationDesc.kind == amp::DataKind::ImageRgbChw) {
        if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
            setup.imageDestinationDesc.type == amp::Tdt::Float32) {
            const bool isFull =
                canUseFullKernelFastPath(setup.imageSourceDesc, setup.imageDestinationDesc);
            if (isFull) {
                didBuild = amp::ImageOps::StretchBlit_Bgra8_Hwc_Full_Rgbf32_Full_Chw(
                    setup.imageSourceDesc, setup.imageDestinationDesc);
            } else {
                didBuild = amp::ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgbf32_Rect_Chw(
                    setup.imageSourceDesc, setup.imageDestinationDesc);
            }
        } else if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
                   setup.imageDestinationDesc.type == amp::Tdt::Float16) {
            didBuild = amp::ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgbf16_Rect_Chw(
                setup.imageSourceDesc, setup.imageDestinationDesc);
        }
    }

    if (setup.imageSourceDesc.kind == amp::DataKind::ImageBgraHwc &&
        setup.imageDestinationDesc.kind == amp::DataKind::ImageRgbHwc) {
        if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
            setup.imageDestinationDesc.type == amp::Tdt::Uint8) {
            didBuild = amp::ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgb8_Rect_Hwc(
                setup.imageSourceDesc, setup.imageDestinationDesc);
        } else if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
                   setup.imageDestinationDesc.type == amp::Tdt::Float32) {
            didBuild = amp::ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgbf32_Rect_Hwc(
                setup.imageSourceDesc, setup.imageDestinationDesc);
        } else if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
                   setup.imageDestinationDesc.type == amp::Tdt::Float16) {
            didBuild = amp::ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgbf16_Rect_Hwc(
                setup.imageSourceDesc, setup.imageDestinationDesc);
        }
    }

    if (setup.imageSourceDesc.kind == amp::DataKind::ImageBgraHwc &&
        setup.imageDestinationDesc.kind == amp::DataKind::ImageGray) {
        if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
            setup.imageDestinationDesc.type == amp::Tdt::Uint8) {
            const bool isFull =
                canUseFullKernelFastPath(setup.imageSourceDesc, setup.imageDestinationDesc);

            if (isFull) {
                didBuild = amp::ImageOps::StretchBlit_Bgra8_Hwc_Full_Gray8_Full(
                    setup.imageSourceDesc, setup.imageDestinationDesc);
            } else {
                didBuild = amp::ImageOps::StretchBlit_Bgra8_Hwc_Rect_Gray8_Rect(
                    setup.imageSourceDesc, setup.imageDestinationDesc);
            }
        }
    }

    if (setup.imageSourceDesc.kind == amp::DataKind::ImageRgbChw &&
        setup.imageDestinationDesc.kind == amp::DataKind::ImageRgbChw) {
        if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
            setup.imageDestinationDesc.type == amp::Tdt::Float32) {
            const bool isFull =
                canUseFullKernelFastPath(setup.imageSourceDesc, setup.imageDestinationDesc);
            if (isFull) {
                didBuild = amp::ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf32_Full_Chw(
                    setup.imageSourceDesc, setup.imageDestinationDesc);
            } else {
                didBuild = amp::ImageOps::StrechBlit_Rgb8_Chw_Rect_Rgbf32_Rect_Chw(
                    setup.imageSourceDesc, setup.imageDestinationDesc);
            }
        } else if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
                   setup.imageDestinationDesc.type == amp::Tdt::Float16) {
            const bool isFull =
                canUseFullKernelFastPath(setup.imageSourceDesc, setup.imageDestinationDesc);
            if (isFull) {
                didBuild = amp::ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf16_Full_Chw(
                    setup.imageSourceDesc, setup.imageDestinationDesc);
            } else {
                didBuild = amp::ImageOps::StrechBlit_Rgb8_Chw_Rect_Rgbf16_Rect_Chw(
                    setup.imageSourceDesc, setup.imageDestinationDesc);
            }
        }
    }

    if (setup.imageSourceDesc.kind == amp::DataKind::ImageRgbChw &&
        setup.imageDestinationDesc.kind == amp::DataKind::ImageRgbHwc) {
        if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
            setup.imageDestinationDesc.type == amp::Tdt::Float32) {
            const bool isFull =
                canUseFullKernelFastPath(setup.imageSourceDesc, setup.imageDestinationDesc);
            if (isFull) {
                didBuild = amp::ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf32_Full_Hwc(
                    setup.imageSourceDesc, setup.imageDestinationDesc);
            } else {
                didBuild = amp::ImageOps::StrechBlit_Rgb8_Chw_Rect_Rgbf32_Rect_Hwc(
                    setup.imageSourceDesc, setup.imageDestinationDesc);
            }
        } else if (setup.imageSourceDesc.type == amp::Tdt::Uint8 &&
                   setup.imageDestinationDesc.type == amp::Tdt::Float16) {
            const bool isFull =
                canUseFullKernelFastPath(setup.imageSourceDesc, setup.imageDestinationDesc);
            if (isFull) {
                didBuild = amp::ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf16_Full_Hwc(
                    setup.imageSourceDesc, setup.imageDestinationDesc);
            } else {
                didBuild = amp::ImageOps::StrechBlit_Rgb8_Chw_Rect_Rgbf16_Rect_Hwc(
                    setup.imageSourceDesc, setup.imageDestinationDesc);
            }
        }
    }

    if (!didBuild) {
        return tl::unexpected(AMP_ERROR(
            amp::ErrorFlag::InvalidData,
            "GenericImageTensorBuilder: unsupported source/destination kind+type conversion"));
    }

    return {};
}
