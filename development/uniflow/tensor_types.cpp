#include "tensor_types.h"

#include <cstring>
#include <cassert> 

template<class TO, class FROM>
static inline TO bit_cast(FROM v) {
    
    static_assert(sizeof(TO) == sizeof(FROM), "size mismatch");
    
    TO out; 
    std::memcpy(&out, &v, sizeof(TO)); 
    return out;
}

// float32 -> fp16 (as 16-bit bits)

uflw::f16 uflw::Float01_Into_f16(float f) {

    uint32_t x = bit_cast<uint32_t>(f);
    uint32_t sign = (x >> 31) & 1;
    int32_t  exp  = int32_t((x >> 23) & 0xFF) - 127 + 15; // rebias
    uint32_t mant = x & 0x7FFFFF;

    if (((x >> 23) & 0xFF) == 0xFF) { // NaN/Inf in f32
        uint16_t h_m = (mant ? 0x200 : 0); // quiet NaN if mant!=0
        return (sign << 15) | (0x1F << 10) | h_m;
    }

    // Round mantissa to 10 bits (add round bit for nearest-even)
    uint32_t mant_rnd = mant + 0x00001000u; // 1<<(23-10-1)

    if (exp >= 0x1F) { // overflow -> Inf
        return (sign << 15) | (0x1F << 10);
    }  
    else if (exp <= 0) { // subnormal/underflow
        if (exp < -10) { // too small -> signed zero
            return uint16_t(sign << 15);
        }
            
        // produce subnormal half: implicit leading 1 then shift
        uint32_t sub = (mant_rnd | 0x00800000u) >> (1 - exp); // 23+1-exp -> align
            
        // keep 10 bits
        return uint16_t((sign << 15) | (sub >> 13));
    } 
    else { // normal half
    
        uint16_t h_exp  = uint16_t(exp & 0x1F);
        uint16_t h_mant = uint16_t((mant_rnd >> 13) & 0x3FF);
    
        // handle mantissa overflow after rounding
        if (h_mant == 0x400) { // carried into 11th bit
            h_mant = 0;
            ++h_exp;
            if (h_exp >= 0x1F) {
                return uint16_t((sign << 15) | (0x1F << 10));
            }
        }
        return uint16_t((sign << 15) | (h_exp << 10) | h_mant);
    }   
}

// fp16 bits -> float32
float uflw::Float01_From_f16(uflw::f16 h) {

  uint32_t sign = (h >> 15) & 1;
  uint32_t exp  = (h >> 10) & 0x1F;
  uint32_t mant = h & 0x3FF;

  uint32_t out;
  if (exp == 0) { // zero or subnormal
    if (mant == 0) {
        out = sign << 31; // +/- 0
    } else {
      // normalize subnormal
      int e = -14; // half bias = 15 → exp = 1-15
      while ((mant & 0x400) == 0) { mant <<= 1; --e; }
      mant &= 0x3FF;
      out = (sign << 31) | uint32_t((e + 127) << 23) | (mant << 13);
    }
    } 
    else if (exp == 0x1F) { // Inf/NaN
        out = (sign << 31) | 0x7F800000u | (mant ? 0x00400000u : 0u);
    } else { // normal
        uint32_t e = exp - 15 + 127; // re-bias to f32
        out = (sign << 31) | (e << 23) | (mant << 13);
    }
  
    return bit_cast<float>(out);
}

float uflw::Float01_From_u8(uflw::u8 v)
{
    return v / 255.0f;
}

uflw::u8 uflw::Float01_Into_u8(float v)
{
    assert(v >= 0.0f && v <= 1.0f);
    return (u8)(v * 255);
}

float uflw::Float01_From_i8(uflw::i8 v)
{
    return Float01_From_u8((u8)(((int)v) + 128));
}

uflw::i8 uflw::Float01_Into_i8(float v)
{
    assert(v >= 0.0f && v <= 1.0f);
    u8 temp = Float01_Into_u8(v);
    return (i8)(-128 + (int)temp);
    return 0;
}

float uflw::Float01_From_f32(uflw::f32 v) { 
    assert(v >= 0.0f && v <= 1.0f);
    return v; 
}

uflw::f32 uflw::Float01_Into_f32(float v) { 
    assert(v >= 0.0f && v <= 1.0f);
    return v; 
}

void uflw::updateRange(QuantizationArgs& qantArgs, ValueType valueType) {
    float rmin = 0.0f, rmax = 1.0f;

    switch (valueType) {
        case ValueType::u8: {
            // u8 integers in [0, 255]
            rmin = qantArgs.scale * (0 - qantArgs.zeroPoint);
            rmax = qantArgs.scale * (255 - qantArgs.zeroPoint);
            break;
        }
        case ValueType::i8: {
            // i8 integers in [-128, 127]
            rmin = qantArgs.scale * (-128 - qantArgs.zeroPoint);
            rmax = qantArgs.scale * (127 - qantArgs.zeroPoint);
            break;
        }
        case ValueType::f16:
        case ValueType::f32: {
            // No quantization implied by type, using a neutral default
            rmin = 0.0f;
            rmax = 1.0f;
            break;
        }
        default:
            rmin = 0.0f; rmax = 1.0f;
    }

    qantArgs.trainRangeMin = rmin;
    qantArgs.trainRangeMax = rmax;
}
