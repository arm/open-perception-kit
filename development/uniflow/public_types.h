#pragma once

#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <cstdarg>
#include <cstdio>

namespace uflw {

    enum class Result {
        Ok = 0,
        ErrorWithSrcSetup,
        ErrorWithDstSetup,
        SizeMismatch
    };

    // Types that build up the tensors.
    // C++ types to use as tensor values and enum values to identify tensor types.
    using u8 = unsigned char;
    using i8 = signed char;
    using f16 = unsigned short;
    using f32 = float;

    enum class ValueType { 
        u8, 
        i8, 
        f16, 
        f32 
    };

    using ValuePointer = void*;

    // Helper functions to convert:
    // Tensor type -> Normalized 0-1 float
    // Normalized 0-1 float -> tensor type
    float Float01_From_f16(f16 h);
    f16 Float01_Into_f16(float f);
    float Float01_From_f32(f32 v);
    f32 Float01_Into_f32(float v);
    float Float01_From_u8(u8 v);
    u8 Float01_Into_u8(float v);
    float Float01_From_i8(i8 v);
    i8 Float01_Into_i8(float v);

    // Shape that holds value count for each dimensions.
    struct Shape {

        explicit Shape(int d0 = 0, int d1 = 0, int d2 = 0, int d3 = 0, int d4 = 0, int d5 = 0, int d6 = 0, int d7 = 0) {
            if(d0 > 0) { valueCount[0] = d0; dimensionCount = 1; }  
            if(d1 > 0) { valueCount[1] = d1; dimensionCount = 2; }  
            if(d2 > 0) { valueCount[2] = d2; dimensionCount = 3; }  
            if(d3 > 0) { valueCount[3] = d3; dimensionCount = 4; }  
            if(d4 > 0) { valueCount[4] = d4; dimensionCount = 5; }  
            if(d5 > 0) { valueCount[5] = d5; dimensionCount = 6; }  
            if(d6 > 0) { valueCount[6] = d6; dimensionCount = 7; }  
            if(d7 > 0) { valueCount[7] = d7; dimensionCount = 8; }  
        }

        uint8_t valueCount[8] = { 0 };
        size_t dimensionCount = 0;
    };

    // Arguments to for value conversions to generate input tensor.
    // Original min/max is the minimum and maximum value of the tensor values used during training.
    struct QuantizationArgs { 

        float trainRangeMin = -1.0f, trainRangeMax = -1.0f;
        float scale = 1.0f; 
        float zeroPoint = 0.0f; 
    };

    enum class ResizeStrategy {
        Disable = 0, // throws Error::SizeMismatch when dimensions do not mach
        ResizeToFill,
        Letterboxing,
        ResizeWithLetterboxing
    };


    // Raw data byte layout
    enum class RawDataFormat {
        Unknown = 0,

        ImageRgb8,
        ImageRgbf32,
        ImageGray8,
        ImageGrayf32,
        
        AudioMonoPcm8,
        AudioMonoPcm16
    
    };

    // In-tensor data layout
    // N=batch, C=channels, H/W=height/width
    // T=time, F=frames (spectrogram)
    enum class TensorLayout  {
        Chw, // [R...][G...][B...]
        Hwc, // [R,G,B][R,G,B][R,G,B] ...

        Nchw, Nhwc, // batch processing (support is TBD)
        
        // N=0: [t0: C0,C1,C2,...] [t1: C0,C1,C2,...] [t2: C0,C1,C2,...]
        // N=1: [t0: C0,C1,C2,...] [t1: C0,C1,C2,...] ...
        Ntc, 
        
        // N=0: [C0: F0,F1,F2,...]
        //      [C1: F0,F1,F2,...]
        // N=1: [C0: F0,F1,F2,...]
        Ncf
    };

    // Underlying engine, to handle the quirks (hack)
    enum class InferenceEngine {
        Unknown = 0,
        OnnxRt,
        Tflm,
        ExecuTorch
    };

    struct ImageTensorBuilderSetup {
        QuantizationArgs quantizationArgs;
        
        TensorLayout dstTensorLayout = TensorLayout::Hwc;
        ValueType dstType = ValueType::f32;
        RawDataFormat srcFormat = RawDataFormat::ImageRgb8;
        
        ResizeStrategy resizeStrategy = ResizeStrategy::Disable;
        bool enableBilinearFiltering = false;

        float letterboxingFillRed = 114 / 255.0f;
        float letterboxingFillGreen = 114 / 255.0f;
        float letterboxingFillBlue = 114 / 255.0f;
    };

    struct ImageTensorBuilder {

        Result buildTensor(const ImageTensorBuilderSetup& setup,
            uint8_t* srcData, size_t srcWidth, size_t srcHeight, size_t srcBufferByteCount, 
            ValuePointer dstData, size_t dstHeight, size_t dstByteCount, size_t dstWidth);

    };

}

