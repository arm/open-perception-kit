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

using namespace opk::stdop::preproc;

namespace {

using ImageKernel = bool (*)(const opk::ImageOpDesc &, const opk::ImageOpDesc &, opk::Sampling);

struct KernelMapping {
    opk::RawImagePixelFormat sourceFormat;
    opk::Dtype sourceType;
    opk::DataKind destinationKind;
    opk::Dtype destinationType;
    ImageKernel kernel;
};

constexpr KernelMapping kKernelMappings[] = {
    {opk::RawImagePixelFormat::Bgra,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbChw,
     opk::Dtype::Float32,
     opk::stdop::preproc::ImageOps::StretchBlit_Bgra8_Hwc_Full_Rgbf32_Full_Chw},
    {opk::RawImagePixelFormat::Bgra,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbChw,
     opk::Dtype::Float16,
     opk::stdop::preproc::ImageOps::StretchBlit_Bgra8_Hwc_Full_Rgbf16_Full_Chw},
    {opk::RawImagePixelFormat::Bgra,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbHwc,
     opk::Dtype::Uint8,
     opk::stdop::preproc::ImageOps::StretchBlit_Bgra8_Hwc_Full_Rgb8_Full_Hwc},
    {opk::RawImagePixelFormat::Bgra,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbHwc,
     opk::Dtype::Float32,
     opk::stdop::preproc::ImageOps::StretchBlit_Bgra8_Hwc_Full_Rgbf32_Full_Hwc},
    {opk::RawImagePixelFormat::Bgra,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbHwc,
     opk::Dtype::Float16,
     opk::stdop::preproc::ImageOps::StretchBlit_Bgra8_Hwc_Full_Rgbf16_Full_Hwc},
    {opk::RawImagePixelFormat::Bgra,
     opk::Dtype::Uint8,
     opk::DataKind::ImageGray,
     opk::Dtype::Uint8,
     opk::stdop::preproc::ImageOps::StretchBlit_Bgra8_Hwc_Full_Gray8_Full},
    {opk::RawImagePixelFormat::Bgra,
     opk::Dtype::Uint8,
     opk::DataKind::ImageGray,
     opk::Dtype::Float32,
     opk::stdop::preproc::ImageOps::StretchBlit_Bgra8_Hwc_Full_Grayf32_Full},
    {opk::RawImagePixelFormat::Rgb,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbChw,
     opk::Dtype::Float32,
     opk::stdop::preproc::ImageOps::StretchBlit_Rgb8_Hwc_Full_Rgbf32_Full_Chw},
    {opk::RawImagePixelFormat::Rgb,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbChw,
     opk::Dtype::Float16,
     opk::stdop::preproc::ImageOps::StretchBlit_Rgb8_Hwc_Full_Rgbf16_Full_Chw},
    {opk::RawImagePixelFormat::Rgb,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbHwc,
     opk::Dtype::Uint8,
     opk::stdop::preproc::ImageOps::StretchBlit_Rgb8_Hwc_Full_Rgb8_Full_Hwc},
    {opk::RawImagePixelFormat::Rgb,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbHwc,
     opk::Dtype::Float32,
     opk::stdop::preproc::ImageOps::StretchBlit_Rgb8_Hwc_Full_Rgbf32_Full_Hwc},
    {opk::RawImagePixelFormat::Rgb,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbHwc,
     opk::Dtype::Float16,
     opk::stdop::preproc::ImageOps::StretchBlit_Rgb8_Hwc_Full_Rgbf16_Full_Hwc},
    {opk::RawImagePixelFormat::Rgb,
     opk::Dtype::Uint8,
     opk::DataKind::ImageGray,
     opk::Dtype::Uint8,
     opk::stdop::preproc::ImageOps::StretchBlit_Rgb8_Hwc_Full_Gray8_Full},
    {opk::RawImagePixelFormat::Rgb,
     opk::Dtype::Uint8,
     opk::DataKind::ImageGray,
     opk::Dtype::Float32,
     opk::stdop::preproc::ImageOps::StretchBlit_Rgb8_Hwc_Full_Grayf32_Full},
    {opk::RawImagePixelFormat::I420,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbChw,
     opk::Dtype::Float32,
     opk::stdop::preproc::ImageOpsI420::StretchBlit_I420_Full_Rgbf32_Full_Chw},
    {opk::RawImagePixelFormat::I420,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbChw,
     opk::Dtype::Float16,
     opk::stdop::preproc::ImageOpsI420::StretchBlit_I420_Full_Rgbf16_Full_Chw},
    {opk::RawImagePixelFormat::I420,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbHwc,
     opk::Dtype::Uint8,
     opk::stdop::preproc::ImageOpsI420::StretchBlit_I420_Full_Rgb8_Full_Hwc},
    {opk::RawImagePixelFormat::I420,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbHwc,
     opk::Dtype::Float32,
     opk::stdop::preproc::ImageOpsI420::StretchBlit_I420_Full_Rgbf32_Full_Hwc},
    {opk::RawImagePixelFormat::I420,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbHwc,
     opk::Dtype::Float16,
     opk::stdop::preproc::ImageOpsI420::StretchBlit_I420_Full_Rgbf16_Full_Hwc},
    {opk::RawImagePixelFormat::I420,
     opk::Dtype::Uint8,
     opk::DataKind::ImageGray,
     opk::Dtype::Uint8,
     opk::stdop::preproc::ImageOpsI420::StretchBlit_I420_Full_Gray8_Full},
    {opk::RawImagePixelFormat::I420,
     opk::Dtype::Uint8,
     opk::DataKind::ImageGray,
     opk::Dtype::Float32,
     opk::stdop::preproc::ImageOpsI420::StretchBlit_I420_Full_Grayf32_Full},
    {opk::RawImagePixelFormat::Nv12,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbChw,
     opk::Dtype::Float32,
     opk::stdop::preproc::ImageOpsNv12::StretchBlit_Nv12_Full_Rgbf32_Full_Chw},
    {opk::RawImagePixelFormat::Nv12,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbChw,
     opk::Dtype::Float16,
     opk::stdop::preproc::ImageOpsNv12::StretchBlit_Nv12_Full_Rgbf16_Full_Chw},
    {opk::RawImagePixelFormat::Nv12,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbHwc,
     opk::Dtype::Uint8,
     opk::stdop::preproc::ImageOpsNv12::StretchBlit_Nv12_Full_Rgb8_Full_Hwc},
    {opk::RawImagePixelFormat::Nv12,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbHwc,
     opk::Dtype::Float32,
     opk::stdop::preproc::ImageOpsNv12::StretchBlit_Nv12_Full_Rgbf32_Full_Hwc},
    {opk::RawImagePixelFormat::Nv12,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbHwc,
     opk::Dtype::Float16,
     opk::stdop::preproc::ImageOpsNv12::StretchBlit_Nv12_Full_Rgbf16_Full_Hwc},
    {opk::RawImagePixelFormat::Nv12,
     opk::Dtype::Uint8,
     opk::DataKind::ImageGray,
     opk::Dtype::Uint8,
     opk::stdop::preproc::ImageOpsNv12::StretchBlit_Nv12_Full_Gray8_Full},
    {opk::RawImagePixelFormat::Nv12,
     opk::Dtype::Uint8,
     opk::DataKind::ImageGray,
     opk::Dtype::Float32,
     opk::stdop::preproc::ImageOpsNv12::StretchBlit_Nv12_Full_Grayf32_Full},
    {opk::RawImagePixelFormat::Yuy2,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbChw,
     opk::Dtype::Float32,
     opk::stdop::preproc::ImageOpsYuy2::StretchBlit_Yuy2_Full_Rgbf32_Full_Chw},
    {opk::RawImagePixelFormat::Yuy2,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbChw,
     opk::Dtype::Float16,
     opk::stdop::preproc::ImageOpsYuy2::StretchBlit_Yuy2_Full_Rgbf16_Full_Chw},
    {opk::RawImagePixelFormat::Yuy2,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbHwc,
     opk::Dtype::Uint8,
     opk::stdop::preproc::ImageOpsYuy2::StretchBlit_Yuy2_Full_Rgb8_Full_Hwc},
    {opk::RawImagePixelFormat::Yuy2,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbHwc,
     opk::Dtype::Float32,
     opk::stdop::preproc::ImageOpsYuy2::StretchBlit_Yuy2_Full_Rgbf32_Full_Hwc},
    {opk::RawImagePixelFormat::Yuy2,
     opk::Dtype::Uint8,
     opk::DataKind::ImageRgbHwc,
     opk::Dtype::Float16,
     opk::stdop::preproc::ImageOpsYuy2::StretchBlit_Yuy2_Full_Rgbf16_Full_Hwc},
    {opk::RawImagePixelFormat::Yuy2,
     opk::Dtype::Uint8,
     opk::DataKind::ImageGray,
     opk::Dtype::Uint8,
     opk::stdop::preproc::ImageOpsYuy2::StretchBlit_Yuy2_Full_Gray8_Full},
    {opk::RawImagePixelFormat::Yuy2,
     opk::Dtype::Uint8,
     opk::DataKind::ImageGray,
     opk::Dtype::Float32,
     opk::stdop::preproc::ImageOpsYuy2::StretchBlit_Yuy2_Full_Grayf32_Full},
};

bool matches(const KernelMapping &mapping,
             const opk::ImageOpDesc &source,
             const opk::ImageOpDesc &destination) {
    return mapping.sourceFormat == source.format && mapping.sourceType == source.type &&
           mapping.destinationKind == destination.kind &&
           mapping.destinationType == destination.type;
}

std::string describeConversion(const opk::ImageOpDesc &source,
                               const opk::ImageOpDesc &destination) {
    return fmt::format("{} {} -> {} {}",
                       magic_enum::enum_name(source.format),
                       magic_enum::enum_name(source.type),
                       magic_enum::enum_name(destination.kind),
                       magic_enum::enum_name(destination.type));
}

} // namespace

opk::Result<void>
opk::stdop::preproc::GenericImageTensorBuilder::build(const TensorBuilder::Setup &setup) {
    for (const auto &mapping : kKernelMappings) {
        if (!matches(mapping, setup.imageSourceDesc, setup.imageDestinationDesc)) {
            continue;
        }

        if (mapping.kernel(
                setup.imageSourceDesc, setup.imageDestinationDesc, opk::Sampling::Nearest)) {
            return {};
        }

        opk::log::error("GenericImageTensorBuilder: conversion kernel failed for {}\n",
                        describeConversion(setup.imageSourceDesc, setup.imageDestinationDesc));
        return tl::unexpected(OPK_ERROR(
            opk::ErrorFlag::InvalidData,
            fmt::format("GenericImageTensorBuilder: conversion kernel failed for {}",
                        describeConversion(setup.imageSourceDesc, setup.imageDestinationDesc))));
    }

    opk::log::error(
        "GenericImageTensorBuilder: unsupported source/destination kind+type conversion: {}\n",
        describeConversion(setup.imageSourceDesc, setup.imageDestinationDesc));
    return tl::unexpected(OPK_ERROR(
        opk::ErrorFlag::InvalidData,
        fmt::format(
            "GenericImageTensorBuilder: unsupported source/destination kind+type conversion: {}",
            describeConversion(setup.imageSourceDesc, setup.imageDestinationDesc))));
}
