/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#include "preproc/GenericImageTensorBuilder.h"
#include "preproc/CpuImageKernels.h"

#include <cassert>
#include <stdint.h>

using namespace pek::preproc;

namespace {

bool canUseFullKernelFastPath(const pek::ImageOpDesc &src, const pek::ImageOpDesc &dst) {
    const bool isSrcFull = src.rectIsFullSurface();
    const bool isDstFull = dst.rectIsFullSurface();
    const bool sameSize =
        (src.surfaceWidth == dst.surfaceWidth) && (src.surfaceHeight == dst.surfaceHeight);
    return isSrcFull && isDstFull && sameSize;
}

} // namespace

pek::Result<void>
pek::preproc::GenericImageTensorBuilder::build(const TensorBuilder::Setup &setup) {
    bool didBuild = false;

    if (setup.imageSourceDesc.kind == pek::DataKind::ImageBgraHwc &&
        setup.imageDestinationDesc.kind == pek::DataKind::ImageRgbChw) {
        if (setup.imageSourceDesc.type == pek::Dtype::Uint8 &&
            setup.imageDestinationDesc.type == pek::Dtype::Float32) {
            const bool isFull =
                canUseFullKernelFastPath(setup.imageSourceDesc, setup.imageDestinationDesc);
            if (isFull) {
                didBuild = pek::preproc::ImageOps::StretchBlit_Bgra8_Hwc_Full_Rgbf32_Full_Chw(
                    setup.imageSourceDesc, setup.imageDestinationDesc);
            } else {
                didBuild = pek::preproc::ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgbf32_Rect_Chw(
                    setup.imageSourceDesc, setup.imageDestinationDesc);
            }
        } else if (setup.imageSourceDesc.type == pek::Dtype::Uint8 &&
                   setup.imageDestinationDesc.type == pek::Dtype::Float16) {
            didBuild = pek::preproc::ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgbf16_Rect_Chw(
                setup.imageSourceDesc, setup.imageDestinationDesc);
        }
    }

    if (setup.imageSourceDesc.kind == pek::DataKind::ImageBgraHwc &&
        setup.imageDestinationDesc.kind == pek::DataKind::ImageRgbHwc) {
        if (setup.imageSourceDesc.type == pek::Dtype::Uint8 &&
            setup.imageDestinationDesc.type == pek::Dtype::Uint8) {
            didBuild = pek::preproc::ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgb8_Rect_Hwc(
                setup.imageSourceDesc, setup.imageDestinationDesc);
        } else if (setup.imageSourceDesc.type == pek::Dtype::Uint8 &&
                   setup.imageDestinationDesc.type == pek::Dtype::Float32) {
            didBuild = pek::preproc::ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgbf32_Rect_Hwc(
                setup.imageSourceDesc, setup.imageDestinationDesc);
        } else if (setup.imageSourceDesc.type == pek::Dtype::Uint8 &&
                   setup.imageDestinationDesc.type == pek::Dtype::Float16) {
            didBuild = pek::preproc::ImageOps::StretchBlit_Bgra8_Hwc_Rect_Rgbf16_Rect_Hwc(
                setup.imageSourceDesc, setup.imageDestinationDesc);
        }
    }

    if (setup.imageSourceDesc.kind == pek::DataKind::ImageBgraHwc &&
        setup.imageDestinationDesc.kind == pek::DataKind::ImageGray) {
        if (setup.imageSourceDesc.type == pek::Dtype::Uint8 &&
            setup.imageDestinationDesc.type == pek::Dtype::Uint8) {
            const bool isFull =
                canUseFullKernelFastPath(setup.imageSourceDesc, setup.imageDestinationDesc);

            if (isFull) {
                didBuild = pek::preproc::ImageOps::StretchBlit_Bgra8_Hwc_Full_Gray8_Full(
                    setup.imageSourceDesc, setup.imageDestinationDesc);
            } else {
                didBuild = pek::preproc::ImageOps::StretchBlit_Bgra8_Hwc_Rect_Gray8_Rect(
                    setup.imageSourceDesc, setup.imageDestinationDesc);
            }
        }
    }

    if (setup.imageSourceDesc.kind == pek::DataKind::ImageRgbChw &&
        setup.imageDestinationDesc.kind == pek::DataKind::ImageRgbChw) {
        if (setup.imageSourceDesc.type == pek::Dtype::Uint8 &&
            setup.imageDestinationDesc.type == pek::Dtype::Float32) {
            const bool isFull =
                canUseFullKernelFastPath(setup.imageSourceDesc, setup.imageDestinationDesc);
            if (isFull) {
                didBuild = pek::preproc::ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf32_Full_Chw(
                    setup.imageSourceDesc, setup.imageDestinationDesc);
            } else {
                didBuild = pek::preproc::ImageOps::StrechBlit_Rgb8_Chw_Rect_Rgbf32_Rect_Chw(
                    setup.imageSourceDesc, setup.imageDestinationDesc);
            }
        } else if (setup.imageSourceDesc.type == pek::Dtype::Uint8 &&
                   setup.imageDestinationDesc.type == pek::Dtype::Float16) {
            const bool isFull =
                canUseFullKernelFastPath(setup.imageSourceDesc, setup.imageDestinationDesc);
            if (isFull) {
                didBuild = pek::preproc::ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf16_Full_Chw(
                    setup.imageSourceDesc, setup.imageDestinationDesc);
            } else {
                didBuild = pek::preproc::ImageOps::StrechBlit_Rgb8_Chw_Rect_Rgbf16_Rect_Chw(
                    setup.imageSourceDesc, setup.imageDestinationDesc);
            }
        }
    }

    if (setup.imageSourceDesc.kind == pek::DataKind::ImageRgbChw &&
        setup.imageDestinationDesc.kind == pek::DataKind::ImageRgbHwc) {
        if (setup.imageSourceDesc.type == pek::Dtype::Uint8 &&
            setup.imageDestinationDesc.type == pek::Dtype::Float32) {
            const bool isFull =
                canUseFullKernelFastPath(setup.imageSourceDesc, setup.imageDestinationDesc);
            if (isFull) {
                didBuild = pek::preproc::ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf32_Full_Hwc(
                    setup.imageSourceDesc, setup.imageDestinationDesc);
            } else {
                didBuild = pek::preproc::ImageOps::StrechBlit_Rgb8_Chw_Rect_Rgbf32_Rect_Hwc(
                    setup.imageSourceDesc, setup.imageDestinationDesc);
            }
        } else if (setup.imageSourceDesc.type == pek::Dtype::Uint8 &&
                   setup.imageDestinationDesc.type == pek::Dtype::Float16) {
            const bool isFull =
                canUseFullKernelFastPath(setup.imageSourceDesc, setup.imageDestinationDesc);
            if (isFull) {
                didBuild = pek::preproc::ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf16_Full_Hwc(
                    setup.imageSourceDesc, setup.imageDestinationDesc);
            } else {
                didBuild = pek::preproc::ImageOps::StrechBlit_Rgb8_Chw_Rect_Rgbf16_Rect_Hwc(
                    setup.imageSourceDesc, setup.imageDestinationDesc);
            }
        }
    }

    if (!didBuild) {
        return tl::unexpected(PEK_ERROR(
            pek::ErrorFlag::InvalidData,
            "GenericImageTensorBuilder: unsupported source/destination kind+type conversion"));
    }

    return {};
}
