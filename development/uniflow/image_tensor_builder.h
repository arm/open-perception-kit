#pragma once

#include "public_types.h"

namespace uflw {

    struct ImageTensorBuilderSetup {
        QuantizationArgs quantizationArgs;
        
        TensorLayout dstTensorLayout = TensorLayout::Hwc;
        ValueType dstType = ValueType::f32;
        RawDataFormat srcFormat = RawDataFormat::ImageRgb8;
        
        ResizeStrategy resizeStrategy = ResizeStrategy::ResizeToFill;
        bool enableBilinearFiltering = false;

        float letterboxingFillRed = 114 / 255.0f;
        float letterboxingFillGreen = 114 / 255.0f;
        float letterboxingFillBlue = 114 / 255.0f;
    };

    struct ImageTensorBuilder {

        static Result buildTensor(const ImageTensorBuilderSetup& setup,
            uint8_t* srcData, size_t srcWidth, size_t srcHeight, size_t srcBufferByteCount, 
            ValuePointer dstData, size_t dstHeight, size_t dstByteCount, size_t dstWidth);

    };

}
