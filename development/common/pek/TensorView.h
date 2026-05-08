/*************************************************************
 * Copyright (C) 2025 Arm Limited. All rights reserved.
 *************************************************************/
#pragma once

#include "pek/Shape.h"
#include "pek/Types.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace pek {

//
// Non-owning wrappers around a memory area contains fixed number of fixed
// type (1, 2 or 4 bytes) tensor values.
//
struct TensorView {

    TensorView() {}

    TensorView(const void *data,
               size_t byteCount,
               pek::Shape shape,
               pek::Tdt type,
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
        valueCount = shape.getFullValueCount();
    }

    float get(size_t i) const {
        assert(i < valueCount);

        switch (type) {
        case pek::Tdt::Uint8:
            return toFloat(*(pek::Uint8 *)(data + i));
        case pek::Tdt::Int8:
            return toFloat(*(pek::Int8 *)(data + i));
        case pek::Tdt::Float16:
            return toFloat(*(pek::Float16 *)(data + i * 2));
        case pek::Tdt::Float32:
            return toFloat(*(pek::Float32 *)(data + i * 4));
        case pek::Tdt::Int64:
            return toFloat(*(pek::Int64 *)(data + i * 8));
        }

        assert(0);
    }

    size_t getCount() const {
        return valueCount;
    }
    size_t getByteCount() const {
        return byteCount;
    }
    pek::Tdt getValueType() const {
        return type;
    }
    pek::Shape getShape() const {
        return shape;
    }
    const uint8_t *getData() const {
        return data;
    }

    float getScale() const {
        return scale;
    }
    float getZeroPoint() const {
        return zeroPoint;
    }

  private:
    inline float toFloat(pek::Int8 v) const {
        return (((float)v) - zeroPoint) * scale;
    }
    inline float toFloat(pek::Uint8 v) const {
        return (((float)v) - zeroPoint) * scale;
    }
    inline float toFloat(pek::Float32 v) const {
        return v;
    }
    inline float toFloat(pek::Float16 v) const {
        return (float)v;
    }
    inline float toFloat(pek::Int64 v) const {
        return (float)v;
    }

    const uint8_t *data;
    size_t byteCount, valueCount;
    pek::Shape shape;

    pek::Tdt type;
    size_t typeSize;

    float scale, zeroPoint;
};

} // namespace pek
