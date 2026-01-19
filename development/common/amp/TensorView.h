#pragma once

#include "amp/Shape.h"
#include "amp/Types.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace amp {

//
// Non-owning wrappers around a memory area contains fixed number of fixed
// type (1, 2 or 4 bytes) tensor values.
//
struct TensorView {

    TensorView(const void *data,
               size_t byteCount,
               amp::Shape shape,
               amp::Tdt type,
               float scale,
               float zeroPoint)
        : data((const uint8_t *)data), byteCount(byteCount), shape(shape), type(type), scale(scale),
          zeroPoint(zeroPoint) {

        switch (type) {
        case Tdt::Int8:
            typeSize = 1;
            break;
        case Tdt::Uint8:
            typeSize = 1;
            break;
        case Tdt::Float16:
            typeSize = 2;
            break;
        case Tdt::Float32:
            typeSize = 4;
            break;
        case Tdt::Int64:
            typeSize = 8;
            break;
        }
        valueCount = byteCount / typeSize;
    }

    float get(size_t i) const {
        assert(i < valueCount);

        switch (type) {
        case amp::Tdt::Uint8:
            return toFloat(*(amp::Uint8 *)(data + i));
        case amp::Tdt::Int8:
            return toFloat(*(amp::Int8 *)(data + i));
        case amp::Tdt::Float16:
            return toFloat(*(amp::Float16 *)(data + i * 2));
        case amp::Tdt::Float32:
            return toFloat(*(amp::Float32 *)(data + i * 4));
        case amp::Tdt::Int64:
            return toFloat(*(amp::Int64 *)(data + i * 8));
        }

        assert(0);
    }

    size_t getCount() const {
        return valueCount;
    }
    size_t getByteCount() const {
        return byteCount;
    }
    amp::Tdt getValueType() const {
        return type;
    }
    amp::Shape getShape() const {
        return shape;
    }
    const uint8_t *getData() const {
        return data;
    }

  private:
    inline float toFloat(amp::Int8 v) const {
        return (((float)v) - zeroPoint) * scale;
    }
    inline float toFloat(amp::Uint8 v) const {
        return (((float)v) - zeroPoint) * scale;
    }
    inline float toFloat(amp::Float32 v) const {
        return v;
    }
    inline float toFloat(amp::Float16 v) const {
        return (float)v;
    }
    inline float toFloat(amp::Int64 v) const {
        return (float)v;
    }

    const uint8_t *data;
    size_t byteCount, valueCount;
    amp::Shape shape;

    amp::Tdt type;
    size_t typeSize;

    float scale, zeroPoint;
};

} // namespace amp
