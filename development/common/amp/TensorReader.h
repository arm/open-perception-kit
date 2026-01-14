#pragma once

#include "amp/Shape.h"
#include "amp/Types.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace amp {

//
// Non-owning wrappers around a memory area contains fixed number of fixed
// type (1, 2 or 4 bytes) tensor values.
//
struct TensorReader {

    TensorReader(const void *data,
                 size_t byteCount,
                 amp::Shape shape,
                 amp::ValueType type,
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
        case amp::ValueType::u8:
            return toFloat(*(amp::u8 *)(data + i));
        case amp::ValueType::i8:
            return toFloat(*(amp::i8 *)(data + i));
        case amp::ValueType::f16:
            return toFloat(*(amp::f16 *)(data + i * 2));
        case amp::ValueType::f32:
            return toFloat(*(amp::f32 *)(data + i * 4));
        case amp::ValueType::i64:
            return toFloat(*(amp::i64 *)(data + i * 8));
        }

        assert(0);
    }

    size_t getCount() const {
        return valueCount;
    }
    size_t getByteCount() const {
        return byteCount;
    }
    amp::ValueType getValueType() const {
        return type;
    }
    amp::Shape getShape() const {
        return shape;
    }
    const uint8_t *getData() const {
        return data;
    }

  private:
    inline float toFloat(amp::i8 v) const {
        return (((float)v) - zeroPoint) * scale;
    }
    inline float toFloat(amp::u8 v) const {
        return (((float)v) - zeroPoint) * scale;
    }
    inline float toFloat(amp::f32 v) const {
        return v;
    }
    inline float toFloat(amp::f16 v) const {
        return (float)v;
    }
    inline float toFloat(amp::i64 v) const {
        return (float)v;
    }

    const uint8_t *data;
    size_t byteCount, valueCount;
    amp::Shape shape;

    amp::ValueType type;
    size_t typeSize;

    float scale, zeroPoint;
};

} // namespace amp
