#pragma once

#include <cstdint>
#include <cstddef>

namespace uf {

    using u8 = unsigned char;
    using i8 = signed char;
    using f16 = unsigned short;
    using f32 = float;

    enum class Type { u8, i8, f16, f32 };
    
    enum class RangeType { Auto, Exact };
    
    struct Range { 
        RangeType type = RangeType::Auto; 
        float min = 0.0f, max = 0.0f; 
    };

    struct Quantization { 
        float scale = 1.0f; 
        int zeroPoint = 0; 
    };

    float Float01_From_f16(f16 h);
    f16 Float01_Into_f16(float f);

    float Float01_From_f32(f32 v);
    f32 Float01_Into_f32(float v);

    float Float01_From_u8(u8 v);
    u8 Float01_Into_u8(float v);

    float Float01_From_i8(i8 v);
    i8 Float01_Into_i8(float v);

    enum class DataByteArrayFormat {
        Image_Rgb888,
        Image_Bgr888,
        Image_Jpeg
    };

    struct TensorFromImage {



    };

}

