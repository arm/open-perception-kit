#pragma once

#include "tensor_types.h"

namespace uflw {

    struct ImageConvert {

        void Resize(uflw::ValuePointer src, size_t srcWidth, size_t srcHeight, RawDataFormat srcFormat,
            uflw::ValuePointer dst, size_t dstWidth, size_t dstHeight, RawDataFormat dstFormat);

    };


}

