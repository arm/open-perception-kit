#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace amp {

//
// Non-owning wrapper around a memory area contains fixed number of fixed
// type (1, 2 or 4 bytes) tensor values.
//

struct TensorReader {

    TensorReader(const void *data,
                 size_t byteCount,
                 uflw::Shape shape,
                 uflw::ValueType type,
                 float scale,
                 float zeroPoint)
        : data((const uint8_t *)data), byteCount(byteCount), shape(shape), type(type), scale(scale),
          zeroPoint(zeroPoint) {

        switch (type) {
        case ValueType::i8:
            typeSize = 1;
            break;
        case ValueType::u8:
            typeSize = 1;
            break;
        case ValueType::f16:
            typeSize = 2;
            break;
        case ValueType::f32:
            typeSize = 4;
            break;
        case ValueType::i64:
            typeSize = 8;
            break;
        }
        valueCount = byteCount / typeSize;
    }

    float get(size_t i) const {
        assert(i < valueCount);

        switch (type) {
        case uflw::ValueType::u8:
            return toFloat(*(uflw::u8 *)(data + i));
        case uflw::ValueType::i8:
            return toFloat(*(uflw::i8 *)(data + i));
        case uflw::ValueType::f16:
            return toFloat(*(uflw::f16 *)(data + i * 2));
        case uflw::ValueType::f32:
            return toFloat(*(uflw::f32 *)(data + i * 4));
        case uflw::ValueType::i64:
            return toFloat(*(uflw::i64 *)(data + i * 8));
        }

        assert(0);
    }

    size_t getCount() const {
        return valueCount;
    }
    size_t getByteCount() const {
        return byteCount;
    }
    uflw::ValueType getValueType() const {
        return type;
    }
    uflw::Shape getShape() const {
        return shape;
    }
    const uint8_t *getData() const {
        return data;
    }

  private:
    inline float toFloat(uflw::i8 v) const {
        return (((float)v) - zeroPoint) * scale;
    }
    inline float toFloat(uflw::u8 v) const {
        return (((float)v) - zeroPoint) * scale;
    }
    inline float toFloat(uflw::f32 v) const {
        return v;
    }
    inline float toFloat(uflw::f16 v) const {
        return (float)v;
    }
    inline float toFloat(uflw::i64 v) const {
        return (float)v;
    }

    const uint8_t *data;
    size_t byteCount, valueCount;
    uflw::Shape shape;

    uflw::ValueType type;
    size_t typeSize;

    float scale, zeroPoint;
};

} // namespace amp

/*
            if constexpr (std::is_same<T, u8>::value) return Float01_From_u8(((T*)data)[i]);
           if constexpr (std::is_same<T, i8>::value)  return Float01_From_i8(((T*)data)[i]);
           if constexpr (std::is_same<T, f16>::value) return Float01_From_f16(((T*)data)[i]);
           if constexpr (std::is_same<T, f32>::value) return Float01_From_f32(((T*)data)[i]);

           //inline float Float01_From_i8(uflw::i8 v) const { return (((float)v) -
   quantizationArgs.zeroPoint) * quantizationArgs.scale; }
       //inline float Float01_From_u8(uflw::u8 v) const { return (((float)v) -
   quantizationArgs.zeroPoint) * quantizationArgs.scale; }
       //inline float Float01_From_f32(uflw::f32 v) const { return v; }
       //inline float Float01_From_f16(uflw::f16 v) { return (uflw::f16)v; }
*/