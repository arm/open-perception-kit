/*************************************************************
 * SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
 *************************************************************/
#pragma once

#include "opk/Shape.h"
#include "opk/Types.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace opk {

/**
 * @brief Non-owning view over tensor memory.
 *
 * TensorView interprets externally owned memory according to shape, element type,
 * and optional quantization parameters.
 */
struct TensorView {

    /** @brief Constructs an empty tensor view. */
    TensorView() = default;

    /**
     * @brief Constructs a view over tensor memory.
     * @param data Pointer to tensor bytes.
     * @param byteCount Total byte size of the backing memory.
     * @param shape Tensor shape.
     * @param type Tensor element type.
     * @param scale Quantization scale used for integer tensors.
     * @param zeroPoint Quantization zero point used for integer tensors.
     */
    TensorView(const void *data,
               size_t byteCount,
               opk::Shape shape,
               opk::Dtype type,
               float scale,
               float zeroPoint)
        : data((const uint8_t *)data), byteCount(byteCount), shape(shape), type(type), scale(scale),
          zeroPoint(zeroPoint) {
        typeSize = getValueTypeByteSize(type);
        valueCount = shape.getFullValueCount();
    }

    /**
     * @brief Returns tensor element @p i converted to float.
     * @param i Flat element index.
     * @return Element value as float, or 0.0f when the index/view is invalid in non-assert builds.
     */
    float get(size_t i) const {
        assert(isValidIndex(i));
        if (!isValidIndex(i)) {
            return 0.0f;
        }

        const size_t offset = i * typeSize;

        switch (type) {
        case opk::Dtype::Uint8:
            return toFloat(loadScalar<opk::Uint8>(offset));
        case opk::Dtype::Int8:
            return toFloat(loadScalar<opk::Int8>(offset));
        case opk::Dtype::Float16:
            return toFloat(loadScalar<opk::Float16>(offset));
        case opk::Dtype::Float32:
            return toFloat(loadScalar<opk::Float32>(offset));
        case opk::Dtype::Int64:
            return toFloat(loadScalar<opk::Int64>(offset));
        }

        assert(0);
        return 0.0f;
    }

    /**
     * @brief Returns true when the view metadata is consistent with the backing memory.
     */
    bool isValid() const {
        if (!data)
            return false;
        if (typeSize == 0)
            return false;

        const size_t requiredBytes = getRequiredByteCount();
        return requiredBytes <= byteCount;
    }

    /** @brief Returns total element count. */
    size_t getCount() const {
        return valueCount;
    }
    /** @brief Returns backing byte count. */
    size_t getByteCount() const {
        return byteCount;
    }
    /** @brief Returns tensor element type. */
    opk::Dtype getValueType() const {
        return type;
    }
    /** @brief Returns tensor shape. */
    opk::Shape getShape() const {
        return shape;
    }
    /** @brief Returns raw backing data pointer. */
    const uint8_t *getData() const {
        return data;
    }

    /** @brief Returns quantization scale. */
    float getScale() const {
        return scale;
    }
    /** @brief Returns quantization zero point. */
    float getZeroPoint() const {
        return zeroPoint;
    }

  private:
    /** @brief Converts signed 8-bit quantized value to float. */
    inline float toFloat(opk::Int8 v) const {
        return (((float)v) - zeroPoint) * scale;
    }
    /** @brief Converts unsigned 8-bit quantized value to float. */
    inline float toFloat(opk::Uint8 v) const {
        return (((float)v) - zeroPoint) * scale;
    }
    /** @brief Converts float32 value to float. */
    inline float toFloat(opk::Float32 v) const {
        return v;
    }
    /** @brief Converts float16 value to float. */
    inline float toFloat(opk::Float16 v) const {
        return (float)v;
    }
    /** @brief Converts int64 value to float. */
    inline float toFloat(opk::Int64 v) const {
        return (float)v;
    }

    /**
     * @brief Loads one scalar value from backing memory using memcpy.
     */
    template <typename T> T loadScalar(size_t offset) const {
        T value{};
        std::memcpy(&value, data + offset, sizeof(T));
        return value;
    }

    /**
     * @brief Returns required byte count implied by current shape and type.
     */
    size_t getRequiredByteCount() const {
        if (typeSize == 0)
            return 0;
        if (valueCount == 0)
            return 0;
        if (valueCount > SIZE_MAX / typeSize)
            return SIZE_MAX;
        return valueCount * typeSize;
    }

    /**
     * @brief Returns true when index @p i can be read safely.
     */
    bool isValidIndex(size_t i) const {
        if (!data)
            return false;
        if (typeSize == 0)
            return false;
        if (byteCount < typeSize)
            return false;
        if (i >= valueCount)
            return false;
        if (i > SIZE_MAX / typeSize)
            return false;

        const size_t offset = i * typeSize;
        return offset <= byteCount - typeSize;
    }

    /// Raw backing memory pointer (non-owning).
    const uint8_t *data = nullptr;
    /// Backing memory size in bytes.
    size_t byteCount = 0;
    /// Total tensor element count.
    size_t valueCount = 0;
    /// Tensor shape metadata.
    opk::Shape shape;

    /// Tensor element type.
    opk::Dtype type = opk::Dtype::Uint8;
    /// Size of one tensor element in bytes.
    size_t typeSize = 1;

    /// Quantization scale.
    float scale = 1.0f;
    /// Quantization zero point.
    float zeroPoint = 0.0f;
};

} // namespace opk
