#pragma once

#include "public_types.h"

namespace uflw {

    struct AudioTensorBuilderSetup {
        QuantizationArgs quantizationArgs;        
        ValueType dstType = ValueType::f32;
        RawDataFormat srcFormat = RawDataFormat::AudioMonoPcm16;        
    };

    struct AudioTensorBuilder {

        static Result buildTensor(const AudioTensorBuilderSetup& setup,
            uint8_t* srcData, size_t srcBufferByteCount, ValuePointer dstData, size_t dstBufferByteCount);

    };

}
