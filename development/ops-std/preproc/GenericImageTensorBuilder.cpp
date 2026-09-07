/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#include "preproc/GenericImageTensorBuilder.h"
#include "preproc/CpuImageKernels.h"
#include "preproc/CpuImageKernelsI420.h"
#include "preproc/CpuImageKernelsNv12.h"
#include "preproc/CpuImageKernelsYuy2.h"

#include "Log.h"
#include <fmt/core.h>
#include <magic_enum/magic_enum.hpp>

#include <string>

using namespace pek::stdop::preproc;

namespace {

using ImageKernel = bool (*)(const pek::ImageOpDesc &, const pek::ImageOpDesc &, pek::Sampling);

struct KernelMapping {
    pek::RawImagePixelFormat sourceFormat;
    pek::Dtype sourceType;
    pek::DataKind destinationKind;
    pek::Dtype destinationType;
    ImageKernel kernel;
};

constexpr KernelMapping kKernelMappings[] = {
    {pek::RawImagePixelFormat::Bgra,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbChw,
     pek::Dtype::Float32,
     pek::stdop::preproc::ImageOps::StretchBlit_Bgra8_Hwc_Full_Rgbf32_Full_Chw},
    {pek::RawImagePixelFormat::Bgra,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbChw,
     pek::Dtype::Float16,
     pek::stdop::preproc::ImageOps::StretchBlit_Bgra8_Hwc_Full_Rgbf16_Full_Chw},
    {pek::RawImagePixelFormat::Bgra,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbHwc,
     pek::Dtype::Uint8,
     pek::stdop::preproc::ImageOps::StretchBlit_Bgra8_Hwc_Full_Rgb8_Full_Hwc},
    {pek::RawImagePixelFormat::Bgra,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbHwc,
     pek::Dtype::Float32,
     pek::stdop::preproc::ImageOps::StretchBlit_Bgra8_Hwc_Full_Rgbf32_Full_Hwc},
    {pek::RawImagePixelFormat::Bgra,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbHwc,
     pek::Dtype::Float16,
     pek::stdop::preproc::ImageOps::StretchBlit_Bgra8_Hwc_Full_Rgbf16_Full_Hwc},
    {pek::RawImagePixelFormat::Bgra,
     pek::Dtype::Uint8,
     pek::DataKind::ImageGray,
     pek::Dtype::Uint8,
     pek::stdop::preproc::ImageOps::StretchBlit_Bgra8_Hwc_Full_Gray8_Full},
    {pek::RawImagePixelFormat::Bgra,
     pek::Dtype::Uint8,
     pek::DataKind::ImageGray,
     pek::Dtype::Float32,
     pek::stdop::preproc::ImageOps::StretchBlit_Bgra8_Hwc_Full_Grayf32_Full},
    {pek::RawImagePixelFormat::Rgb,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbChw,
     pek::Dtype::Float32,
     pek::stdop::preproc::ImageOps::StretchBlit_Rgb8_Hwc_Full_Rgbf32_Full_Chw},
    {pek::RawImagePixelFormat::Rgb,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbChw,
     pek::Dtype::Float16,
     pek::stdop::preproc::ImageOps::StretchBlit_Rgb8_Hwc_Full_Rgbf16_Full_Chw},
    {pek::RawImagePixelFormat::Rgb,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbHwc,
     pek::Dtype::Uint8,
     pek::stdop::preproc::ImageOps::StretchBlit_Rgb8_Hwc_Full_Rgb8_Full_Hwc},
    {pek::RawImagePixelFormat::Rgb,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbHwc,
     pek::Dtype::Float32,
     pek::stdop::preproc::ImageOps::StretchBlit_Rgb8_Hwc_Full_Rgbf32_Full_Hwc},
    {pek::RawImagePixelFormat::Rgb,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbHwc,
     pek::Dtype::Float16,
     pek::stdop::preproc::ImageOps::StretchBlit_Rgb8_Hwc_Full_Rgbf16_Full_Hwc},
    {pek::RawImagePixelFormat::Rgb,
     pek::Dtype::Uint8,
     pek::DataKind::ImageGray,
     pek::Dtype::Uint8,
     pek::stdop::preproc::ImageOps::StretchBlit_Rgb8_Hwc_Full_Gray8_Full},
    {pek::RawImagePixelFormat::Rgb,
     pek::Dtype::Uint8,
     pek::DataKind::ImageGray,
     pek::Dtype::Float32,
     pek::stdop::preproc::ImageOps::StretchBlit_Rgb8_Hwc_Full_Grayf32_Full},
    {pek::RawImagePixelFormat::I420,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbChw,
     pek::Dtype::Float32,
     pek::stdop::preproc::ImageOpsI420::StretchBlit_I420_Full_Rgbf32_Full_Chw},
    {pek::RawImagePixelFormat::I420,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbChw,
     pek::Dtype::Float16,
     pek::stdop::preproc::ImageOpsI420::StretchBlit_I420_Full_Rgbf16_Full_Chw},
    {pek::RawImagePixelFormat::I420,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbHwc,
     pek::Dtype::Uint8,
     pek::stdop::preproc::ImageOpsI420::StretchBlit_I420_Full_Rgb8_Full_Hwc},
    {pek::RawImagePixelFormat::I420,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbHwc,
     pek::Dtype::Float32,
     pek::stdop::preproc::ImageOpsI420::StretchBlit_I420_Full_Rgbf32_Full_Hwc},
    {pek::RawImagePixelFormat::I420,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbHwc,
     pek::Dtype::Float16,
     pek::stdop::preproc::ImageOpsI420::StretchBlit_I420_Full_Rgbf16_Full_Hwc},
    {pek::RawImagePixelFormat::I420,
     pek::Dtype::Uint8,
     pek::DataKind::ImageGray,
     pek::Dtype::Uint8,
     pek::stdop::preproc::ImageOpsI420::StretchBlit_I420_Full_Gray8_Full},
    {pek::RawImagePixelFormat::I420,
     pek::Dtype::Uint8,
     pek::DataKind::ImageGray,
     pek::Dtype::Float32,
     pek::stdop::preproc::ImageOpsI420::StretchBlit_I420_Full_Grayf32_Full},
    {pek::RawImagePixelFormat::Nv12,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbChw,
     pek::Dtype::Float32,
     pek::stdop::preproc::ImageOpsNv12::StretchBlit_Nv12_Full_Rgbf32_Full_Chw},
    {pek::RawImagePixelFormat::Nv12,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbChw,
     pek::Dtype::Float16,
     pek::stdop::preproc::ImageOpsNv12::StretchBlit_Nv12_Full_Rgbf16_Full_Chw},
    {pek::RawImagePixelFormat::Nv12,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbHwc,
     pek::Dtype::Uint8,
     pek::stdop::preproc::ImageOpsNv12::StretchBlit_Nv12_Full_Rgb8_Full_Hwc},
    {pek::RawImagePixelFormat::Nv12,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbHwc,
     pek::Dtype::Float32,
     pek::stdop::preproc::ImageOpsNv12::StretchBlit_Nv12_Full_Rgbf32_Full_Hwc},
    {pek::RawImagePixelFormat::Nv12,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbHwc,
     pek::Dtype::Float16,
     pek::stdop::preproc::ImageOpsNv12::StretchBlit_Nv12_Full_Rgbf16_Full_Hwc},
    {pek::RawImagePixelFormat::Nv12,
     pek::Dtype::Uint8,
     pek::DataKind::ImageGray,
     pek::Dtype::Uint8,
     pek::stdop::preproc::ImageOpsNv12::StretchBlit_Nv12_Full_Gray8_Full},
    {pek::RawImagePixelFormat::Nv12,
     pek::Dtype::Uint8,
     pek::DataKind::ImageGray,
     pek::Dtype::Float32,
     pek::stdop::preproc::ImageOpsNv12::StretchBlit_Nv12_Full_Grayf32_Full},
    {pek::RawImagePixelFormat::Yuy2,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbChw,
     pek::Dtype::Float32,
     pek::stdop::preproc::ImageOpsYuy2::StretchBlit_Yuy2_Full_Rgbf32_Full_Chw},
    {pek::RawImagePixelFormat::Yuy2,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbChw,
     pek::Dtype::Float16,
     pek::stdop::preproc::ImageOpsYuy2::StretchBlit_Yuy2_Full_Rgbf16_Full_Chw},
    {pek::RawImagePixelFormat::Yuy2,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbHwc,
     pek::Dtype::Uint8,
     pek::stdop::preproc::ImageOpsYuy2::StretchBlit_Yuy2_Full_Rgb8_Full_Hwc},
    {pek::RawImagePixelFormat::Yuy2,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbHwc,
     pek::Dtype::Float32,
     pek::stdop::preproc::ImageOpsYuy2::StretchBlit_Yuy2_Full_Rgbf32_Full_Hwc},
    {pek::RawImagePixelFormat::Yuy2,
     pek::Dtype::Uint8,
     pek::DataKind::ImageRgbHwc,
     pek::Dtype::Float16,
     pek::stdop::preproc::ImageOpsYuy2::StretchBlit_Yuy2_Full_Rgbf16_Full_Hwc},
    {pek::RawImagePixelFormat::Yuy2,
     pek::Dtype::Uint8,
     pek::DataKind::ImageGray,
     pek::Dtype::Uint8,
     pek::stdop::preproc::ImageOpsYuy2::StretchBlit_Yuy2_Full_Gray8_Full},
    {pek::RawImagePixelFormat::Yuy2,
     pek::Dtype::Uint8,
     pek::DataKind::ImageGray,
     pek::Dtype::Float32,
     pek::stdop::preproc::ImageOpsYuy2::StretchBlit_Yuy2_Full_Grayf32_Full},
};

bool matches(const KernelMapping &mapping,
             const pek::ImageOpDesc &source,
             const pek::ImageOpDesc &destination) {
    return mapping.sourceFormat == source.format && mapping.sourceType == source.type &&
           mapping.destinationKind == destination.kind &&
           mapping.destinationType == destination.type;
}

std::string describeConversion(const pek::ImageOpDesc &source,
                               const pek::ImageOpDesc &destination) {
    return fmt::format("{} {} -> {} {}",
                       magic_enum::enum_name(source.format),
                       magic_enum::enum_name(source.type),
                       magic_enum::enum_name(destination.kind),
                       magic_enum::enum_name(destination.type));
}

} // namespace

pek::Result<void>
pek::stdop::preproc::GenericImageTensorBuilder::build(const TensorBuilder::Setup &setup) {
    for (const auto &mapping : kKernelMappings) {
        if (!matches(mapping, setup.imageSourceDesc, setup.imageDestinationDesc)) {
            continue;
        }

        if (mapping.kernel(
                setup.imageSourceDesc, setup.imageDestinationDesc, pek::Sampling::Nearest)) {
            return {};
        }

        pek::log::error("GenericImageTensorBuilder: conversion kernel failed for {}\n",
                        describeConversion(setup.imageSourceDesc, setup.imageDestinationDesc));
        return tl::unexpected(PEK_ERROR(
            pek::ErrorFlag::InvalidData,
            fmt::format("GenericImageTensorBuilder: conversion kernel failed for {}",
                        describeConversion(setup.imageSourceDesc, setup.imageDestinationDesc))));
    }

    pek::log::error(
        "GenericImageTensorBuilder: unsupported source/destination kind+type conversion: {}\n",
        describeConversion(setup.imageSourceDesc, setup.imageDestinationDesc));
    return tl::unexpected(PEK_ERROR(
        pek::ErrorFlag::InvalidData,
        fmt::format(
            "GenericImageTensorBuilder: unsupported source/destination kind+type conversion: {}",
            describeConversion(setup.imageSourceDesc, setup.imageDestinationDesc))));
}
