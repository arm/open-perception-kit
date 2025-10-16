#pragma once

#include <cstdint>
#include <cstddef>
#include <cassert>
#include <type_traits>

#include "tensor_types.h"

namespace uflw {

    //
    // Non-owning wrapper around a memory area contains fixed number of fixed 
    // type (1 or 2 bytes) tensor values.
    //

    template<typename T>
    struct TensorView {

        TensorView(void* data, size_t count) : data((std::byte*)data), count(count) {
            this->typeSize = sizeof(T); 
        }

        T& operator[](std::size_t i) { 
            assert(i < count);
            return *(T*)((std::byte*)data + i * typeSize); 
        }

        const T& operator[](std::size_t i) const { 
            assert(i < count);
            return *(const T*)((const std::byte*)data + i * typeSize); 
        }
  
        float get01(std::size_t i) const {
            assert(i < count);
            if constexpr (std::is_same<T, u8>::value) return Float01_From_u8((*this)[i]);
            if constexpr (std::is_same<T, i8>::value)  return Float01_From_i8((*this)[i]);
            if constexpr (std::is_same<T, f16>::value) return Float01_From_f16((*this)[i]);
            if constexpr (std::is_same<T, f32>::value) return Float01_From_f32((*this)[i]);
        }

        void set01(std::size_t i, float v) {
            assert(i < count);
            if constexpr (std::is_same<T, u8>::value) (*this)[i] = Float01_Into_u8(v);
            if constexpr (std::is_same<T, i8>::value) (*this)[i] = Float01_Into_i8(v);
            if constexpr (std::is_same<T, f16>::value) (*this)[i] = Float01_Into_f16(v);
            if constexpr (std::is_same<T, f32>::value) (*this)[i] = Float01_Into_f32(v);
        }

    private:

        ValueType valueType;
        std::byte* data;
        size_t count, typeSize;

    };

}