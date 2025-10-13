#pragma once

#include <cstdint>
#include <cstddef>

namespace uf {

    using u8 = unsigned char;
    using i8 = signed char;
    using f16 = unsigned short;
    using f32 = float;

    enum class Type { u8, i8, f16, f32 };
    enum class Range { R0_1, Rm1_1 };

    float Float01_From_f16(f16 h);
    f16 Float01_Into_f16(float f);

    float Float01_From_f32(f32 v);
    f32 Float01_Into_f32(float v);

    float Float01_From_u8(u8 v);
    u8 Float01_Into_u8(float v);

    float Float01_From_i8(i8 v);
    i8 Float01_Into_i8(float v);

    enum class Ordering { Nchw, Nhwc, Chw, Hwc };

}