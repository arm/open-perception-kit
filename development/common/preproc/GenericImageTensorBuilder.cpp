#include "preproc/GenericImageTensorBuilder.h"
#include "preproc/CpuImageKernels.h"

#include <stdint.h>

using namespace amp;

amp::Result<void> amp::GenericImageTensorBuilder::build(const TensorBuilder::Setup &setup) {

    const uint8_t *src = setup.imageSource.data;
    size_t srcWidth = setup.imageSource.width;
    size_t srcHeight = setup.imageSource.height;
    size_t srcByteCount = setup.imageSource.byteCount;

    uint8_t *dst = setup.imageDestination.data;
    size_t dstWidth = setup.imageDestination.width;
    size_t dstHeight = setup.imageDestination.height;
    size_t dstByteCount = setup.imageDestination.byteCount;

    if (setup.imageSource.kind == amp::TensorDataKind::ImageRgbChw &&
        setup.imageDestination.kind == amp::TensorDataKind::ImageRgbChw) {

        if (setup.imageSource.type == amp::ValueType::u8 &&
            setup.imageDestination.type == amp::ValueType::f32) {
            amp::ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf32_Full_Chw(
                src, srcWidth, srcHeight, (float *)dst, dstWidth, dstHeight);
        }
    }

    if (setup.imageSource.kind == amp::TensorDataKind::ImageRgbChw &&
        setup.imageDestination.kind == amp::TensorDataKind::ImageRgbHwc) {

        if (setup.imageSource.type == amp::ValueType::u8 &&
            setup.imageDestination.type == amp::ValueType::f32) {
            amp::ImageOps::StrechBlit_Rgb8_Chw_Full_Rgbf32_Full_Hwc(
                src, srcWidth, srcHeight, (float *)dst, dstWidth, dstHeight);
        }
    }

    return {};
}
